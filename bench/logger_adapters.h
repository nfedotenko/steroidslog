/*
 * Copyright(c) 2025-2026 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 *
 * Backend adapters for the benchmark harness. Fairness rules:
 *  - identical message set for every logger (the log_* functions below);
 *  - the same sink for every logger: one file, passed to init() — the
 *    harness defaults to /dev/null;
 *  - each logger in its recommended low-latency configuration: quill gets a
 *    cached logger pointer (a per-call Frontend::get_logger() name lookup
 *    costs hundreds of ns and is NOT representative) and a busy-spinning
 *    backend; fmtlog gets a polling thread; spdlog is synchronous by design
 *    and is measured as such;
 *  - dropped() reports records the logger discarded during the run, so
 *    throughput results cannot silently hide losses. steroidslog (Drop
 *    policy) counts drops; the other three block or allocate instead and
 *    report 0 by construction.
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

//==============================================================================

#include <steroidslog/steroidslog.h>

namespace steroidslog_adapter {

inline void init(const std::string& file) {
    // steroidslog's sink is std::cout; point it at the same file the other
    // backends write to.
    static std::ofstream out;
    static std::once_flag once;
    std::call_once(once, [&] {
        out.open(file, std::ios::trunc);
        std::cout.rdbuf(out.rdbuf());
        (void)steroidslog::Logger::instance(); // start the consumer
    });
}

inline uint64_t dropped() {
    return steroidslog::Logger::instance().dropped_count();
}

inline void log_static() {
    STERLOG_INFO("Starting backup replica garbage collector thread");
}

inline void log_string_concat(std::string_view s) {
    STERLOG_INFO("Opened session with {}", s);
}

inline void log_single_int(int a) {
    STERLOG_INFO("Backup storage speeds (min): {} MB/s read", a);
}

inline void log_two_ints(int a, int b) {
    STERLOG_INFO("buffer consumed {} bytes, alloc: {}", a, b);
}

inline void log_single_double(double x) {
    STERLOG_INFO("Using tombstone ratio balancer with ratio = {:.3f}", x);
}

inline void log_complex(int a, int b, double d) {
    STERLOG_INFO("Init buffers: {} receive ({} MB), took {:.1f} ms", a, b, d);
}

} // namespace steroidslog_adapter

//==============================================================================

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace spdlog_adapter {

inline void init(const std::string& file) {
    static std::once_flag once;
    std::call_once(once, [&] {
        auto sink =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        auto logger = std::make_shared<spdlog::logger>("bench", std::move(sink));
        logger->set_pattern("%v");
        logger->set_level(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    });
}

inline uint64_t dropped() { return 0; } // synchronous: nothing to drop

inline void log_static() {
    SPDLOG_INFO("Starting backup replica garbage collector thread");
}

inline void log_string_concat(std::string_view s) {
    SPDLOG_INFO("Opened session with {}", s);
}

inline void log_single_int(int a) {
    SPDLOG_INFO("Backup storage speeds (min): {} MB/s read", a);
}

inline void log_two_ints(int a, int b) {
    SPDLOG_INFO("buffer consumed {} bytes, alloc: {}", a, b);
}

inline void log_single_double(double x) {
    SPDLOG_INFO("Using tombstone ratio balancer with ratio = {:.3f}", x);
}

inline void log_complex(int a, int b, double d) {
    SPDLOG_INFO("Init buffers: {} receive ({} MB), took {:.1f} ms", a, b, d);
}

} // namespace spdlog_adapter

//==============================================================================

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>

namespace quill_adapter {

// Cached once in init(): quill's recommended usage. The previous harness
// called Frontend::get_logger("bench") (a locked name lookup) on every log
// call, which inflated quill's numbers by an order of magnitude.
inline quill::Logger* g_logger = nullptr;

inline void init(const std::string& file) {
    static std::once_flag once;
    std::call_once(once, [&] {
        quill::BackendOptions bo;
        bo.sleep_duration = std::chrono::nanoseconds{0}; // busy spin
        quill::Backend::start(bo);

        auto sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
            file,
            [] {
                quill::FileSinkConfig cfg;
                cfg.set_open_mode('w');
                return cfg;
            }(),
            quill::FileEventNotifier{});

        quill::PatternFormatterOptions fmt_opts{
            "%(message)", "%H:%M:%S.%Qns", quill::Timezone::GmtTime};
        g_logger = quill::Frontend::create_or_get_logger(
            "bench", std::move(sink), fmt_opts);
    });
}

inline uint64_t dropped() { return 0; } // default queue blocks, not drops

inline void log_static() {
    QUILL_LOG_INFO(g_logger,
                   "Starting backup replica garbage collector thread");
}

inline void log_string_concat(std::string_view s) {
    QUILL_LOG_INFO(g_logger, "Opened session with {}", s);
}

inline void log_single_int(int a) {
    QUILL_LOG_INFO(g_logger, "Backup storage speeds (min): {} MB/s read", a);
}

inline void log_two_ints(int a, int b) {
    QUILL_LOG_INFO(g_logger, "buffer consumed {} bytes, alloc: {}", a, b);
}

inline void log_single_double(double x) {
    QUILL_LOG_INFO(g_logger,
                   "Using tombstone ratio balancer with ratio = {:.3f}", x);
}

inline void log_complex(int a, int b, double d) {
    QUILL_LOG_INFO(g_logger,
                   "Init buffers: {} receive ({} MB), took {:.1f} ms", a, b, d);
}

} // namespace quill_adapter

//==============================================================================

#include <fmtlog.h>

namespace fmtlog_adapter {

inline void init(const std::string& file) {
    static std::once_flag once;
    std::call_once(once, [&] {
        fmtlog::setHeaderPattern("");     // message only
        fmtlog::setLogLevel(fmtlog::INF);
        fmtlog::setLogFile(file.c_str(), /*truncate=*/true);
        fmtlog::startPollingThread(1000000 /*ns*/);
    });
}

inline uint64_t dropped() { return 0; } // allocates instead of dropping

inline void log_static() {
    logi("Starting backup replica garbage collector thread");
}

inline void log_string_concat(std::string_view s) {
    logi("Opened session with {}", s);
}

inline void log_single_int(int a) {
    logi("Backup storage speeds (min): {} MB/s read", a);
}

inline void log_two_ints(int a, int b) {
    logi("buffer consumed {} bytes, alloc: {}", a, b);
}

inline void log_single_double(double x) {
    logi("Using tombstone ratio balancer with ratio = {:.3f}", x);
}

inline void log_complex(int a, int b, double d) {
    logi("Init buffers: {} receive ({} MB), took {:.1f} ms", a, b, d);
}

} // namespace fmtlog_adapter

//==============================================================================
// Runtime dispatch: one backend per process run.
//==============================================================================

struct Adapter {
    std::string_view name;
    void (*init)(const std::string& file);
    uint64_t (*dropped)();
    void (*log_static)();
    void (*log_string_concat)(std::string_view);
    void (*log_single_int)(int);
    void (*log_two_ints)(int, int);
    void (*log_single_double)(double);
    void (*log_complex)(int, int, double);
};

inline constexpr Adapter ADAPTERS[] = {
    {"steroidslog", steroidslog_adapter::init, steroidslog_adapter::dropped,
     steroidslog_adapter::log_static, steroidslog_adapter::log_string_concat,
     steroidslog_adapter::log_single_int, steroidslog_adapter::log_two_ints,
     steroidslog_adapter::log_single_double, steroidslog_adapter::log_complex},
    {"spdlog", spdlog_adapter::init, spdlog_adapter::dropped,
     spdlog_adapter::log_static, spdlog_adapter::log_string_concat,
     spdlog_adapter::log_single_int, spdlog_adapter::log_two_ints,
     spdlog_adapter::log_single_double, spdlog_adapter::log_complex},
    {"quill", quill_adapter::init, quill_adapter::dropped,
     quill_adapter::log_static, quill_adapter::log_string_concat,
     quill_adapter::log_single_int, quill_adapter::log_two_ints,
     quill_adapter::log_single_double, quill_adapter::log_complex},
    {"fmtlog", fmtlog_adapter::init, fmtlog_adapter::dropped,
     fmtlog_adapter::log_static, fmtlog_adapter::log_string_concat,
     fmtlog_adapter::log_single_int, fmtlog_adapter::log_two_ints,
     fmtlog_adapter::log_single_double, fmtlog_adapter::log_complex},
};

inline const Adapter* find_adapter(std::string_view name) {
    for (const Adapter& a : ADAPTERS) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}
