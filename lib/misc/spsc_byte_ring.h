/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#define SL_CACHELINE 64

namespace steroidslog {

//------------------------------------------------------------------------------
// SPSC ring of bytes carrying variable-length records.
//
// Producer side:  byte* p = ring.reserve(n); memcpy into p; ring.commit();
// Consumer side:  auto rec = ring.front(); ...; ring.pop();
//
// Every record starts with a uint32_t holding its total size in bytes
// (including the size field itself) and must be a multiple of 4, so records
// always begin at 4-byte-aligned offsets. When a record does not fit in the
// space remaining before the end of the buffer, the producer writes a WRAP
// marker there and the record starts at offset 0 instead; the consumer skips
// over the marker transparently.
//------------------------------------------------------------------------------
template <std::size_t ReqCap>
requires(ReqCap >= 64 && (ReqCap & (ReqCap - 1)) == 0)
class spsc_byte_ring {
    static constexpr std::size_t Capacity = ReqCap;
    static constexpr std::size_t Mask = Capacity - 1;
    static constexpr uint32_t WRAP_MARKER = 0xFFFFFFFFu;

public:
    spsc_byte_ring() noexcept = default;
    spsc_byte_ring(const spsc_byte_ring&) = delete;
    spsc_byte_ring& operator=(const spsc_byte_ring&) = delete;
    spsc_byte_ring(spsc_byte_ring&&) = delete;
    spsc_byte_ring& operator=(spsc_byte_ring&&) = delete;

    // Producer. Returns a writable span of n bytes, or nullptr when the ring
    // does not have room. n must be a non-zero multiple of 4 and at most
    // Capacity / 2. The reservation becomes visible to the consumer only
    // after commit().
    std::byte* reserve(std::size_t n) noexcept {
        if (n == 0 || (n & 3u) != 0 || n > Capacity / 2) {
            return nullptr;
        }

        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t to_end = Capacity - tail;
        // Bytes this reservation consumes: the record itself plus, when it
        // has to wrap, the skipped stretch at the end of the buffer.
        const std::size_t need = (n <= to_end) ? n : to_end + n;

        // Keep 4 bytes of slack so that a full ring never looks empty.
        std::size_t used = (tail - head_cache_) & Mask;
        if (used + need > Capacity - 4) {
            head_cache_ = head_.load(std::memory_order_acquire);
            used = (tail - head_cache_) & Mask;
            if (used + need > Capacity - 4) {
                return nullptr; // full
            }
        }

        if (n <= to_end) {
            pending_pos_ = tail;
        } else {
            // to_end >= 4 because tail is always 4-aligned.
            const uint32_t marker = WRAP_MARKER;
            std::memcpy(buf_ + tail, &marker, sizeof(marker));
            pending_pos_ = 0;
        }
        pending_end_ = (pending_pos_ + n) & Mask;
        return buf_ + pending_pos_;
    }

    // Producer. Publishes the bytes written since the last reserve().
    void commit() noexcept {
        tail_.store(pending_end_, std::memory_order_release);
    }

    // Consumer. Returns the oldest committed record (starting with its size
    // field), or an empty span when the ring has no records.
    std::span<const std::byte> front() noexcept {
        std::size_t head = head_.load(std::memory_order_relaxed);
        for (;;) {
            if (head == tail_cache_) {
                tail_cache_ = tail_.load(std::memory_order_acquire);
                if (head == tail_cache_) {
                    return {}; // empty
                }
            }
            uint32_t size = 0;
            std::memcpy(&size, buf_ + head, sizeof(size));
            if (size == WRAP_MARKER) {
                head = 0;
                head_.store(0, std::memory_order_release);
                continue;
            }
            return {buf_ + head, size};
        }
    }

    // Consumer. Releases the record returned by the preceding front().
    void pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        uint32_t size = 0;
        std::memcpy(&size, buf_ + head, sizeof(size));
        head_.store((head + size) & Mask, std::memory_order_release);
    }

    // Approximate: exact only when called from the consumer thread.
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    // Producer-owned line: position published to the consumer plus the
    // producer's cached view of the consumer position and the pending
    // reservation bounds.
    alignas(SL_CACHELINE) std::atomic<std::size_t> tail_{0};
    std::size_t head_cache_{0};
    std::size_t pending_pos_{0};
    std::size_t pending_end_{0};

    // Consumer-owned line.
    alignas(SL_CACHELINE) std::atomic<std::size_t> head_{0};
    std::size_t tail_cache_{0};

    alignas(SL_CACHELINE) std::byte buf_[Capacity];
};

} // namespace steroidslog

#undef SL_CACHELINE
