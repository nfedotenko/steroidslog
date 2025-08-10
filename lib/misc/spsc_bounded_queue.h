/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#define SL_CACHELINE 64

namespace steroidslog {

template <typename T, std::size_t ReqCap>
requires(ReqCap >= 2 && (ReqCap & (ReqCap - 1)) == 0)
class spsc_bounded_queue {
    static constexpr std::size_t Capacity = ReqCap;
    static constexpr std::size_t Mask = Capacity - 1;

public:
    spsc_bounded_queue() noexcept
        : head_(0), tail_(0), head_cache_(0), tail_cache_(0) {}

    spsc_bounded_queue(spsc_bounded_queue&&) = delete;
    spsc_bounded_queue& operator=(spsc_bounded_queue&&) = delete;
    ~spsc_bounded_queue() { clear(); }

    spsc_bounded_queue(const spsc_bounded_queue&) = delete;
    spsc_bounded_queue& operator=(const spsc_bounded_queue&) = delete;

    bool enqueue(const T& item) noexcept { return emplace_impl(item); }

    bool enqueue(T&& item) noexcept { return emplace_impl(std::move(item)); }

    template <class... Args>
    bool emplace(Args&&... args) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & Mask;

        if (next == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next == head_cache_) {
                return false; // full
            }
        }

        ::new (static_cast<void*>(buf_[tail].bytes))
            T(std::forward<Args>(args)...);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool dequeue(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_) {
                return false; // empty
            }
        }

        T* ptr = std::launder(reinterpret_cast<T*>(buf_[head].bytes));
        out = std::move(*ptr);
        ptr->~T();

        head_.store((head + 1) & Mask, std::memory_order_release);
        return true;
    }

    void clear() noexcept {
        T tmp;
        while (dequeue(tmp)) {}
    }

    // size() is only approximate under concurrency, but handy for drain.
    std::size_t approx_size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (t + Capacity - h) & Mask;
    }

private:
    template <class U>
    bool emplace_impl(U&& v) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & Mask;

        if (next == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next == head_cache_) {
                return false;
            }
        }

        ::new (static_cast<void*>(std::addressof(buf_[tail])))
            T(std::forward<U>(v));
        tail_.store(next, std::memory_order_release);
        return true;
    }

    alignas(SL_CACHELINE) std::atomic<std::size_t> head_;
    alignas(SL_CACHELINE) std::atomic<std::size_t> tail_;
    alignas(SL_CACHELINE) std::size_t head_cache_;
    alignas(SL_CACHELINE) std::size_t tail_cache_;

    struct Cell {
        alignas(T) std::byte bytes[sizeof(T)];
    };
    Cell buf_[Capacity];
};

} // namespace steroidslog

#undef SL_CACHELINE
