/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <benchmark/benchmark.h>

#include "steroidslog/steroidslog.h"

// Prevent the compiler from optimizing away results
using benchmark::ClobberMemory;
using benchmark::DoNotOptimize;

// pseudo_map lookup: measure ID-lambda lookup cost
static void BM_PseudoMapGet(benchmark::State& state) {
    constexpr auto id = []() constexpr { return "fmt {}"; };
    for (auto _ : state) {
        auto s = pseudo_map::get(id); // first call initializes, thereafter fast lookup
        DoNotOptimize(s);
        ClobberMemory();
    }
}
BENCHMARK(BM_PseudoMapGet)->Threads(1)->Threads(4);

// Enqueue with no formatting arguments
static void BM_EnqueueNoArgs(benchmark::State& state) {
    for (auto _ : state) {
        LOG_DEBUG("noop"); // zero-arg log
    }
}
BENCHMARK(BM_EnqueueNoArgs)->Threads(1)->Threads(4);

// Enqueue with a single integer argument
static void BM_EnqueueOneArg(benchmark::State& state) {
    for (auto _ : state) {
        LOG_INFO("value: {}", 123); // one-arg log
    }
}
BENCHMARK(BM_EnqueueOneArg)->Threads(1)->Threads(4);

// Synchronous formatting baseline
static void BM_SyncFormat(benchmark::State& state) {
    int x = 456;
    for (auto _ : state) {
        auto s = std::vformat("sync {}", std::make_format_args(x)); // raw vformat cost
        DoNotOptimize(s);
        ClobberMemory();
    }
}
BENCHMARK(BM_SyncFormat);

// TODO:: Compare with spdlog, nanolog & fmtlog

BENCHMARK_MAIN();
