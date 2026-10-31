/*
 * Copyright(c) 2025-2026 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 *
 * Per-call latency and throughput harness.
 *
 * Methodology:
 *  - One backend per process: an idle backend's worker thread would perturb
 *    another backend's numbers, so the run script launches this binary once
 *    per logger.
 *  - Every logger writes to the same kind of sink: a file (default
 *    /dev/null). steroidslog's stream is redirected to that file too.
 *  - Latency is the producer-side cost of one log call, measured with
 *    serialized timestamps (lfence+rdtsc / rdtscp+lfence on x86, isb+cntvct
 *    on ARM64) around each call; ticks are converted to nanoseconds with
 *    the same calibration the logger itself uses.
 *  - The report is a percentile summary (p50/p90/p99/p99.9/p99.99/max) over
 *    all samples plus the number of records the logger dropped during the
 *    run; raw samples can be dumped to CSV. Throughput is a separate mode.
 */

#include "logger_adapters.h"

#include <misc/tsc_clock.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace {

//------------------------------------------------------------------------------
// Serialized tick reads around the measured call
//------------------------------------------------------------------------------
inline uint64_t ts_begin() noexcept {
#if defined(__x86_64__) || defined(__i386__) ||                               \
    (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
    _mm_lfence();
    return __rdtsc();
#elif defined(__aarch64__) && !defined(__APPLE__)
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
    // Apple Silicon and other platforms: rdticks() resolves to the cheapest
    // reliable monotonic read there. Smoke-test quality, not publishable.
    return steroidslog::rdticks();
#endif
}

inline uint64_t ts_end() noexcept {
#if defined(__x86_64__) || defined(__i386__) ||                               \
    (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
    unsigned aux = 0;
    const uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#elif defined(__aarch64__) && !defined(__APPLE__)
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
#else
    return steroidslog::rdticks();
#endif
}

void pin_this_thread(int cpu) {
#if defined(__linux__)
    if (cpu < 0) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned>(cpu) % CPU_SETSIZE, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpu; // affinity is a Linux-only concern for published numbers
#endif
}

//------------------------------------------------------------------------------
// Cases: identical messages for every backend (defined in logger_adapters.h)
//------------------------------------------------------------------------------
struct Case {
    std::string_view name;
    void (*call)(const Adapter&);
};

constexpr Case CASES[] = {
    {"staticString", [](const Adapter& a) { a.log_static(); }},
    {"stringConcat",
     [](const Adapter& a) {
         a.log_string_concat("basic+udp:host=192.168.1.140,port=12246");
     }},
    {"singleInteger", [](const Adapter& a) { a.log_single_int(181); }},
    {"twoIntegers",
     [](const Adapter& a) { a.log_two_ints(1032024, 1016544); }},
    {"singleDouble", [](const Adapter& a) { a.log_single_double(0.4); }},
    {"complexFormat",
     [](const Adapter& a) { a.log_complex(50000, 97, 26.2); }},
};

struct Options {
    std::string backend;
    std::string sink_file;
    std::string case_filter = "all";
    std::string raw_dir;        // dump per-sample CSVs here when set
    std::string summary_csv;    // append summary rows here when set
    std::size_t samples = 200'000;
    std::size_t warmup = 20'000;
    // Latency mode paces the producer (bursts with a gap) so the queue never
    // saturates: a saturated run measures the overflow path, not the call.
    // Saturation behavior is what --throughput reports (with drop counts).
    std::size_t burst = 20;
    std::size_t gap_ns = 100'000;
    int threads = 1;
    int cpu_base = -1;          // first producer CPU; -1 = no pinning
    bool throughput = false;
};

uint64_t percentile(const std::vector<uint64_t>& sorted, double q) {
    if (sorted.empty()) {
        return 0;
    }
    const double pos = q * static_cast<double>(sorted.size() - 1);
    return sorted[static_cast<std::size_t>(pos + 0.5)];
}

void run_case(const Options& opt, const Adapter& adapter, const Case& bc,
              const steroidslog::tsc_calibration& calib) {
    const std::size_t per_thread = opt.samples / static_cast<std::size_t>(
                                                     opt.threads);
    std::vector<std::vector<uint64_t>> samples(
        static_cast<std::size_t>(opt.threads));

    const uint64_t drops_before = adapter.dropped();
    const auto wall0 = std::chrono::steady_clock::now();

    auto worker = [&](int t) {
        pin_this_thread(opt.cpu_base < 0 ? -1 : opt.cpu_base + t);
        auto& out = samples[static_cast<std::size_t>(t)];

        const uint64_t warm_gap = static_cast<uint64_t>(
            static_cast<double>(opt.gap_ns) / calib.ns_per_tick);
        for (std::size_t i = 0; i < opt.warmup; ++i) {
            bc.call(adapter);
            if (!opt.throughput && opt.burst != 0 &&
                (i + 1) % opt.burst == 0) {
                const uint64_t until = ts_end() + warm_gap;
                while (ts_end() < until) {
                }
            }
        }
        if (opt.throughput) {
            for (std::size_t i = 0; i < per_thread; ++i) {
                bc.call(adapter);
            }
            return;
        }
        const uint64_t gap_ticks = static_cast<uint64_t>(
            static_cast<double>(opt.gap_ns) / calib.ns_per_tick);
        out.resize(per_thread);
        for (std::size_t i = 0; i < per_thread; ++i) {
            const uint64_t t0 = ts_begin();
            bc.call(adapter);
            const uint64_t t1 = ts_end();
            out[i] = t1 - t0;
            if (opt.burst != 0 && (i + 1) % opt.burst == 0) {
                const uint64_t until = t1 + gap_ticks;
                while (ts_end() < until) {
                    // busy wait: keep the core, do not sleep
                }
            }
        }
    };

    if (opt.threads == 1) {
        worker(0);
    } else {
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(opt.threads));
        for (int t = 0; t < opt.threads; ++t) {
            threads.emplace_back(worker, t);
        }
        for (auto& th : threads) {
            th.join();
        }
    }

    const auto wall1 = std::chrono::steady_clock::now();
    const uint64_t drops = adapter.dropped() - drops_before;
    const std::size_t total = per_thread *
                              static_cast<std::size_t>(opt.threads);

    if (opt.throughput) {
        const double sec =
            std::chrono::duration<double>(wall1 - wall0).count();
        const double mps = sec > 0 ? static_cast<double>(total) / sec / 1e6
                                   : 0.0;
        std::printf("%-12s %-14s threads=%d msgs=%zu drops=%llu "
                    "throughput=%.2f M msgs/s\n",
                    opt.backend.c_str(), std::string(bc.name).c_str(),
                    opt.threads, total,
                    static_cast<unsigned long long>(drops), mps);
        if (!opt.summary_csv.empty()) {
            std::ofstream csv(opt.summary_csv, std::ios::app);
            csv << opt.backend << ',' << bc.name << ',' << opt.threads
                << ",throughput," << total << ',' << drops << ',' << mps
                << '\n';
        }
        return;
    }

    // Merge, convert to ns, sort once.
    std::vector<uint64_t> ns;
    ns.reserve(total);
    for (const auto& s : samples) {
        for (const uint64_t ticks : s) {
            ns.push_back(static_cast<uint64_t>(
                static_cast<double>(ticks) * calib.ns_per_tick));
        }
    }
    std::sort(ns.begin(), ns.end());

    double mean = 0;
    for (const uint64_t v : ns) {
        mean += static_cast<double>(v);
    }
    mean /= ns.empty() ? 1 : static_cast<double>(ns.size());

    const uint64_t p50 = percentile(ns, 0.50);
    const uint64_t p90 = percentile(ns, 0.90);
    const uint64_t p99 = percentile(ns, 0.99);
    const uint64_t p999 = percentile(ns, 0.999);
    const uint64_t p9999 = percentile(ns, 0.9999);
    const uint64_t mx = ns.empty() ? 0 : ns.back();

    std::printf("%-12s %-14s threads=%d samples=%zu drops=%llu  "
                "p50=%llu p90=%llu p99=%llu p99.9=%llu p99.99=%llu "
                "max=%llu mean=%.1f (ns)\n",
                opt.backend.c_str(), std::string(bc.name).c_str(),
                opt.threads, ns.size(),
                static_cast<unsigned long long>(drops),
                static_cast<unsigned long long>(p50),
                static_cast<unsigned long long>(p90),
                static_cast<unsigned long long>(p99),
                static_cast<unsigned long long>(p999),
                static_cast<unsigned long long>(p9999),
                static_cast<unsigned long long>(mx), mean);

    if (!opt.summary_csv.empty()) {
        std::ofstream csv(opt.summary_csv, std::ios::app);
        csv << opt.backend << ',' << bc.name << ',' << opt.threads
            << ",latency," << ns.size() << ',' << drops << ',' << p50 << ','
            << p90 << ',' << p99 << ',' << p999 << ',' << p9999 << ',' << mx
            << ',' << mean << '\n';
    }
    if (!opt.raw_dir.empty()) {
        std::ofstream raw(opt.raw_dir + "/" + opt.backend + "_" +
                          std::string(bc.name) + "_t" +
                          std::to_string(opt.threads) + ".csv");
        raw << "ns\n";
        for (const uint64_t v : ns) {
            raw << v << '\n';
        }
    }
}

bool parse_size(std::string_view s, std::size_t& out) {
    const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{} && res.ptr == s.data() + s.size();
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
#if defined(_WIN32)
    opt.sink_file = "NUL";
#else
    opt.sink_file = "/dev/null";
#endif

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> std::string_view {
            return i + 1 < argc ? std::string_view(argv[++i])
                                : std::string_view{};
        };
        if (a == "--backend") {
            opt.backend = next();
        } else if (a == "--case") {
            opt.case_filter = next();
        } else if (a == "--sink-file") {
            opt.sink_file = next();
        } else if (a == "--samples") {
            parse_size(next(), opt.samples);
        } else if (a == "--warmup") {
            parse_size(next(), opt.warmup);
        } else if (a == "--burst") {
            parse_size(next(), opt.burst);
        } else if (a == "--gap-ns") {
            parse_size(next(), opt.gap_ns);
        } else if (a == "--threads") {
            std::size_t t = 1;
            parse_size(next(), t);
            opt.threads = static_cast<int>(t);
        } else if (a == "--cpu-base") {
            std::size_t c = 0;
            parse_size(next(), c);
            opt.cpu_base = static_cast<int>(c);
        } else if (a == "--raw-dir") {
            opt.raw_dir = next();
        } else if (a == "--summary-csv") {
            opt.summary_csv = next();
        } else if (a == "--throughput") {
            opt.throughput = true;
        } else {
            std::fprintf(stderr,
                         "usage: %s --backend steroidslog|spdlog|quill|fmtlog"
                         " [--case NAME|all] [--samples N] [--warmup N]"
                         " [--threads T] [--cpu-base N] [--sink-file PATH]"
                         " [--summary-csv PATH] [--raw-dir DIR]"
                         " [--throughput]\n",
                         argv[0]);
            return 2;
        }
    }

    const Adapter* adapter = find_adapter(opt.backend);
    if (!adapter) {
        std::fprintf(stderr, "unknown --backend '%s'\n",
                     opt.backend.c_str());
        return 2;
    }

    // Calibrate before the backend starts spinning.
    const auto calib = steroidslog::tsc_calibration::measure();
    adapter->init(opt.sink_file);
    // Let the backend finish its own startup (steroidslog's consumer spends
    // ~10 ms calibrating; quill/fmtlog spin up worker threads) so warmup and
    // drop counts reflect steady state.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool ran = false;
    for (const Case& c : CASES) {
        if (opt.case_filter == "all" || opt.case_filter == c.name) {
            run_case(opt, *adapter, c, calib);
            ran = true;
        }
    }
    if (!ran) {
        std::fprintf(stderr, "unknown --case '%s'\n",
                     opt.case_filter.c_str());
        return 2;
    }
    return 0;
}
