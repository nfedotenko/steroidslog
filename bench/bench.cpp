/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <benchmark/benchmark.h>

#include "steroidslog/steroidslog.h"

using namespace steroidslog;

// Prevent the compiler from optimizing away results
using benchmark::ClobberMemory;
using benchmark::DoNotOptimize;

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

// TODO:: Compare with spdlog, nanolog & fmtlog

BENCHMARK_MAIN();
