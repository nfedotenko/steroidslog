/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */
#include <benchmark/benchmark.h>

#include "logger_adapters.h"

#if defined(_WIN32)
#include <winbase.h>
#elif defined (__linux__)
#include <pthread.h>
#include <sched.h>
#endif

inline void pin_this_thread(unsigned cpu_index) {
#if defined(_WIN32)
    const DWORD_PTR mask = (1ull << (cpu_index % (8 * sizeof(DWORD_PTR))));
    SetThreadAffinityMask(GetCurrentThread(), mask);
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_index % CPU_SETSIZE, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpu_index;
#endif
}

template <Backend B>
static void BM_staticString(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    for (auto _ : st) {
        log_static<B>();
        benchmark::DoNotOptimize(_);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

template <Backend B>
static void BM_stringConcat(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    std::string s = "basic+udp:host=192.168.1.140,port=12246";
    for (auto _ : st) {
        log_string_concat<B>(s);
        benchmark::DoNotOptimize(s);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

template <Backend B>
static void BM_singleInteger(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    int a = 181;
    for (auto _ : st) {
        log_single_int<B>(a);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

template <Backend B>
static void BM_twoIntegers(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    int a = 1032024, b = 1016544;
    for (auto _ : st) {
        log_two_ints<B>(a, b);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

template <Backend B>
static void BM_singleDouble(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    double x = 0.4;
    for (auto _ : st) {
        log_single_double<B>(x);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

template <Backend B>
static void BM_complexFormat(benchmark::State& st) {
    pin_this_thread(st.thread_index());
    init<B>(false);
    int a = 50000, b = 97;
    double d = 26.2;
    for (auto _ : st) {
        log_complex<B>(a, b, d);
    }
    st.counters["msgs/s"] =
        benchmark::Counter(st.iterations(), benchmark::Counter::kIsRate);
}

#define REG(backend)                                                           \
    BENCHMARK_TEMPLATE(BM_staticString, backend);                              \
    BENCHMARK_TEMPLATE(BM_stringConcat, backend);                              \
    BENCHMARK_TEMPLATE(BM_singleInteger, backend);                             \
    BENCHMARK_TEMPLATE(BM_twoIntegers, backend);                               \
    BENCHMARK_TEMPLATE(BM_singleDouble, backend);                              \
    BENCHMARK_TEMPLATE(BM_complexFormat, backend);

#if 1
REG(Backend::Steroidslog)
REG(Backend::Spdlog)
REG(Backend::Quill)
REG(Backend::Fmtlog)
#endif

#define REG_THREADS(backend, T)                                                \
    BENCHMARK_TEMPLATE(BM_staticString, backend)->Threads(T);                  \
    BENCHMARK_TEMPLATE(BM_stringConcat, backend)->Threads(T);                  \
    BENCHMARK_TEMPLATE(BM_singleInteger, backend)->Threads(T);                 \
    BENCHMARK_TEMPLATE(BM_twoIntegers, backend)->Threads(T);                   \
    BENCHMARK_TEMPLATE(BM_singleDouble, backend)->Threads(T);                  \
    BENCHMARK_TEMPLATE(BM_complexFormat, backend)->Threads(T);

#if 1
REG_THREADS(Backend::Steroidslog, 4)
REG_THREADS(Backend::Spdlog, 4)
//REG_THREADS(Backend::Quill, 4)
REG_THREADS(Backend::Fmtlog, 4)
#endif

BENCHMARK_MAIN();
