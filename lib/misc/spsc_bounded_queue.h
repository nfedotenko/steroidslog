/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

template <typename T, size_t ReqCap>
class spsc_bounded_queue {
    static_assert((ReqCap & (ReqCap - 1)) == 0,
                  "Capacity must be power of two");

public:
    spsc_bounded_queue() noexcept
        : head_(0), tail_(0), head_cache_(0), tail_cache_(0) {}

    bool enqueue(const T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & Mask;
        // Refill head cache only on potential full
        if (next_tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_)
                return false;
        }
        buffer_[tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool enqueue(T&& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & Mask;
        if (next_tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_)
                return false;
        }
        buffer_[tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool dequeue(T& out) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_)
                return false;
        }
        out = std::move(buffer_[head]);
        head_.store((head + 1) & Mask, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

private:
    static constexpr size_t Capacity = ReqCap;
    static constexpr size_t Mask = Capacity - 1;
    static constexpr size_t CACHE_LINE_SIZE = 64;
    alignas(CACHE_LINE_SIZE) T buffer_[Capacity];
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_, tail_;
    size_t head_cache_, tail_cache_;
};
