/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

//==============================================================================

#include <steroidslog/steroidslog.h>

namespace steroidslog_adapter {

inline void init(bool to_file = false, std::string file = {}) {}

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
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

namespace spdlog_adapter {

inline void init(bool to_file = false, std::string file = {}) {
    static std::once_flag once;
    std::call_once(once, [] {
        spdlog::set_pattern("%v");
        spdlog::set_level(spdlog::level::info);
    });

    if (auto lg = spdlog::get("bench")) {
        spdlog::set_default_logger(std::move(lg));
        return;
    }

    try {
        std::shared_ptr<spdlog::logger> logger;
        if (to_file) {
            if (file.empty()) {
                file = "spdlog_bench.log";
            }
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
            logger = std::make_shared<spdlog::logger>("bench", std::move(sink));
        } else {
            auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
            logger = std::make_shared<spdlog::logger>("bench", std::move(sink));
        }
        spdlog::register_logger(logger);
        spdlog::set_default_logger(std::move(logger));
    } catch (const spdlog::spdlog_ex& e) {
        // Fallback to a null sink
        auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        auto fallback =
            std::make_shared<spdlog::logger>("bench_fallback", std::move(sink));
        spdlog::register_logger(fallback);
        spdlog::set_default_logger(std::move(fallback));
    }
}

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
#include <quill/sinks/NullSink.h>

namespace quill_adapter {

inline void init(bool to_file = false, std::string file = {}) {
    static std::once_flag started;
    std::call_once(started, [] {
        quill::Backend::start();
    });

    std::shared_ptr<quill::Sink> sink;
    if (to_file) {
        if (file.empty()) {
            file = "quill_bench.log";
        }
        // Minimal FileSink config; open in write mode.
        sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
            file,
            [] {
                quill::FileSinkConfig cfg;
                cfg.set_open_mode('w');
                return cfg;
            }(),
            quill::FileEventNotifier{});
    } else {
        // Console (stdout) sink with default config
        sink = quill::Frontend::create_or_get_sink<quill::NullSink>(
            "bench_console");
    }

    // Minimal pattern like spdlog's "%v" => message only
    quill::PatternFormatterOptions fmt_opts{
        "%(message)",            // pattern
        "%H:%M:%S.%Qns",         // time format (unused with message-only)
        quill::Timezone::GmtTime // tz
    };

    // Create (or fetch) our named logger wired to the sink
    quill::Logger* logger = quill::Frontend::create_or_get_logger(
        "bench", std::move(sink), fmt_opts);
    std::ignore = logger;
}

// Helper to fetch the logger we created in init()
inline quill::Logger* get_logger() {
    return quill::Frontend::get_logger("bench");
}

inline void log_static() {
    QUILL_LOG_INFO(get_logger(),
                   "Starting backup replica garbage collector thread");
}

inline void log_string_concat(std::string_view s) {
    QUILL_LOG_INFO(get_logger(), "Opened session with {}", s);
}

inline void log_single_int(int a) {
    QUILL_LOG_INFO(get_logger(), "Backup storage speeds (min): {} MB/s read", a);
}

inline void log_two_ints(int a, int b) {
    QUILL_LOG_INFO(get_logger(), "buffer consumed {} bytes, alloc: {}", a, b);
}

inline void log_single_double(double x) {
    QUILL_LOG_INFO(get_logger(),
                   "Using tombstone ratio balancer with ratio = {:.3f}", x);
}

inline void log_complex(int a, int b, double d) {
    QUILL_LOG_INFO(get_logger(),
                   "Init buffers: {} receive ({} MB), took {:.1f} ms", a, b, d);
}

} // namespace quill_adapter

//==============================================================================

#include <fmtlog.h>

namespace fmtlog_adapter {

inline void init(bool to_file = false, std::string file = {}) {
    static std::once_flag once;
    std::call_once(once, [] {
        fmtlog::setHeaderPattern("");               // message only
        fmtlog::setLogLevel(fmtlog::INF);           // ensure INFO is enabled
        fmtlog::startPollingThread(5000000 /*ns*/); // 5ms background poller
    });

    if (to_file) {
        if (file.empty()) {
            file = "fmtlog_bench.log";
        }
        // Truncate = true for reproducible runs
        fmtlog::setLogFile(file.c_str(), true);
    } else {
        // Null sink: override output with a no-op callback
        //fmtlog::setLogCB(&LogCBFn, fmtlog::DBG);
        //fmtlog::closeLogFile(); // ensure file isn’t active
    }
}

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

enum class Backend { Steroidslog, Spdlog, Quill, Fmtlog };

template <Backend B>
static void init(bool to_file) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::init(to_file, "steroids.log");
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::init(to_file, "spd.log");
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::init(to_file, "quill.log");
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::init(to_file, "fmt.log");
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_static() {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_static();
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_static();
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_static();
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_static();
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_string_concat(std::string_view s) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_string_concat(s);
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_string_concat(s);
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_string_concat(s);
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_string_concat(s);
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_single_int(int a) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_single_int(a);
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_single_int(a);
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_single_int(a);
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_single_int(a);
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_two_ints(int a, int b) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_two_ints(a, b);
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_two_ints(a, b);
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_two_ints(a, b);
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_two_ints(a, b);
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_single_double(double x) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_single_double(x);
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_single_double(x);
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_single_double(x);
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_single_double(x);
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}

template <Backend B>
static void log_complex(int a, int b, double d) {
    if constexpr (B == Backend::Steroidslog) {
        steroidslog_adapter::log_complex(a, b, d);
    } else if constexpr (B == Backend::Spdlog) {
        spdlog_adapter::log_complex(a, b, d);
    } else if constexpr (B == Backend::Quill) {
        quill_adapter::log_complex(a, b, d);
    } else if constexpr (B == Backend::Fmtlog) {
        fmtlog_adapter::log_complex(a, b, d);
    } else {
        static_assert(false, "Selected backend is not implemented.");
    }
}
