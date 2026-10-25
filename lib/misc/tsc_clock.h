/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#elif defined(__APPLE__)
#include <time.h>
#endif

namespace steroidslog {

// Cheapest available monotonic tick counter for the producer hot path:
// TSC on x86, the generic counter on ARM64, steady_clock elsewhere. Tick
// units differ per platform; tsc_calibration converts them to wall time.
inline uint64_t rdticks() noexcept {
#if defined(__x86_64__) || defined(__i386__) ||                               \
    (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
    return __rdtsc();
#elif defined(__APPLE__)
    // cntvct_el0 is unreliable from EL0 on Apple Silicon; this is the
    // canonical cheap monotonic read there (~20 ns).
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#elif defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Measures tick frequency against steady_clock and anchors ticks to the
// system clock, so producer-side tick stamps can be printed as wall time.
struct tsc_calibration {
    uint64_t base_ticks{0};
    double ns_per_tick{1.0};
    std::chrono::system_clock::time_point base_wall{};

    static tsc_calibration measure(
        std::chrono::milliseconds window = std::chrono::milliseconds(10)) {
        tsc_calibration c;
        c.base_wall = std::chrono::system_clock::now();
        const auto s0 = std::chrono::steady_clock::now();
        c.base_ticks = rdticks();

        std::this_thread::sleep_for(window);

        const uint64_t t1 = rdticks();
        const auto s1 = std::chrono::steady_clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            s1 - s0)
                            .count();
        if (t1 > c.base_ticks && ns > 0) {
            c.ns_per_tick = static_cast<double>(ns) /
                            static_cast<double>(t1 - c.base_ticks);
        }
        return c;
    }

    std::chrono::system_clock::time_point
    to_wall(uint64_t ticks) const noexcept {
        const double dt =
            (static_cast<double>(ticks) - static_cast<double>(base_ticks)) *
            ns_per_tick;
        return base_wall +
               std::chrono::duration_cast<
                   std::chrono::system_clock::duration>(
                   std::chrono::nanoseconds(std::llround(dt)));
    }
};

} // namespace steroidslog
