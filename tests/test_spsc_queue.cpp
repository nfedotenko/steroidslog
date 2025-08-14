/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#if __has_include(<misc/spsc_bounded_queue.h>)
#include <misc/spsc_bounded_queue.h>
#define STEROIDSLOG_HAVE_SPSC 1
#else
#define STEROIDSLOG_HAVE_SPSC 0
#endif

using namespace std::chrono_literals;
using namespace steroidslog;

TEST(SPSC, BasicEnqueueDequeue) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    spsc_bounded_queue<int, 8> q;
    EXPECT_TRUE(q.enqueue(1));
    int v = 0;
    EXPECT_TRUE(q.dequeue(v));
    EXPECT_EQ(v, 1);
#endif
}

// A helper non-trivial type that is default-constructible and move-assignable
struct NonTrivial {
    static inline std::atomic<int> live{0};
    int x;

    NonTrivial() : x(0) { live.fetch_add(1); }
    explicit NonTrivial(int v) : x(v) { live.fetch_add(1); }
    NonTrivial(const NonTrivial& o) : x(o.x) { live.fetch_add(1); }
    NonTrivial(NonTrivial&& o) noexcept : x(o.x) { live.fetch_add(1); }
    NonTrivial& operator=(const NonTrivial&) = default;
    NonTrivial& operator=(NonTrivial&&) noexcept = default;
    ~NonTrivial() { live.fetch_sub(1); }
};

TEST(SPSC, EmplaceConstructsAndDestroys) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    NonTrivial::live = 0;
    {
        spsc_bounded_queue<NonTrivial, 4> q;
        EXPECT_TRUE(q.emplace(7));
        NonTrivial out{0};
        EXPECT_TRUE(q.dequeue(out));
        EXPECT_EQ(out.x, 7);
        q.clear();
    }
    EXPECT_EQ(NonTrivial::live.load(), 0);
#endif
}

TEST(SPSC, QueueFullReturnsFalse) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    spsc_bounded_queue<int, 2> q;
    // Capacity 2 -> can hold at most 1 element in this implementation.
    EXPECT_TRUE(q.enqueue(1));
    EXPECT_FALSE(q.enqueue(2)); // should report full
    int v;
    EXPECT_TRUE(q.dequeue(v));
    EXPECT_EQ(v, 1);
#endif
}

TEST(SPSC, WrapAroundCorrectness) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    spsc_bounded_queue<int, 4> q;
    for (int i = 0; i < 8; ++i) {
        while (!q.enqueue(i)) {
            std::this_thread::yield();
        }
        int v;
        while (!q.dequeue(v)) {
            std::this_thread::yield();
        }
        EXPECT_EQ(v, i);
    }
#endif
}

TEST(SPSC, ProducerConsumerMany) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    spsc_bounded_queue<int, 1024> q;
    const int N = 5000;
    std::atomic<int> sum{0};
    std::thread prod([&] {
        for (int i = 1; i <= N; ++i) {
            while (!q.enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });
    std::thread cons([&] {
        int v;
        for (int i = 0; i < N;) {
            if (q.dequeue(v)) {
                sum.fetch_add(v);
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });
    prod.join();
    cons.join();
    EXPECT_EQ(sum.load(), N * (N + 1) / 2);
#endif
}

TEST(SPSC, ApproxSizeSingleThread) {
#if !STEROIDSLOG_HAVE_SPSC
    GTEST_SKIP() << "spsc_bounded_queue header not found; skipping.";
#else
    spsc_bounded_queue<int, 8> q;
    EXPECT_EQ(q.approx_size(), 0u);
    EXPECT_TRUE(q.enqueue(1));
    EXPECT_GT(q.approx_size(), 0u);
    int v;
    EXPECT_TRUE(q.dequeue(v));
    EXPECT_EQ(q.approx_size(), 0u);
#endif
}
