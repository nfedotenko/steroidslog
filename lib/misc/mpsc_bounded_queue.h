/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename T>
concept QueueElement =
    std::is_move_constructible_v<T> && std::is_default_constructible_v<T>;

template <std::size_t N>
concept PowerOfTwo = (N > 0) && ((N & (N - 1)) == 0); // TODO

inline constexpr std::size_t CACHE_LINE_SIZE = 64;

template <QueueElement T, /*PowerOfTwo*/ std::size_t Capacity>
class mpsc_bounded_queue final {
    static_assert(Capacity >= 2, "Capacity must be ≥2");

    using index_t =
        std::conditional_t<(Capacity <= UINT32_MAX), uint32_t, std::size_t>;
    static constexpr index_t mask = Capacity - 1;

    struct alignas(CACHE_LINE_SIZE) Slot final {
        std::atomic<index_t> seq;
        alignas(alignof(T)) std::byte storage[sizeof(T)];

        T* ptr() noexcept {
            return std::launder(reinterpret_cast<T*>(&storage));
        }

        template <typename U>
        void
        construct(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
            std::construct_at(ptr(), std::forward<U>(v));
        }

        void destroy() noexcept(std::is_nothrow_destructible_v<T>) {
            std::destroy_at(ptr());
        }
    };

public:
    mpsc_bounded_queue() {
        for (index_t i = 0; i < Capacity; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    ~mpsc_bounded_queue() {
        T tmp;
        while (try_pop(tmp)) {
        }
    }

    template <typename U>
        requires std::convertible_to<U, T>
    bool try_push(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        const index_t ticket = tail_.fetch_add(1, std::memory_order_relaxed);
        Slot& s = buffer_[ticket & mask];

        // wait for consumer to advance past this slot
        while (s.seq.load(std::memory_order_acquire) != ticket) {
            __builtin_ia32_pause();
        }

        s.construct(std::forward<U>(v));
        s.seq.store(ticket + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                  std::is_nothrow_destructible_v<T>) {
        const index_t head = head_.load(std::memory_order_relaxed);
        Slot& s = buffer_[head & mask];

        if (s.seq.load(std::memory_order_acquire) != head + 1) {
            return false;
        }

        out = std::move(*s.ptr());
        s.destroy();
        s.seq.store(head + Capacity, std::memory_order_release);
        head_.store(head + 1, std::memory_order_relaxed);
        return true;
    }

private:
    alignas(CACHE_LINE_SIZE) std::atomic<index_t> tail_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<index_t> head_{0};

    alignas(CACHE_LINE_SIZE) Slot buffer_[Capacity];
};
