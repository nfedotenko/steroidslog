/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "misc/spsc_bounded_queue.h"

#include <atomic>
#include <cstring>
#include <format>
#include <iostream>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

namespace steroidslog {

enum class LogLevel : uint8_t { Debug, Info, Warning, Error, Unknown };

namespace pseudomap {

template <typename Identifier> struct Entry {
    inline static bool init = false;
    inline static std::string_view value;
};

template <typename Identifier> std::string_view& get(Identifier id) {
    if (!Entry<Identifier>::init) {
        Entry<Identifier>::value = id();
        Entry<Identifier>::init = true;
    }
    return Entry<Identifier>::value;
}

} // namespace pseudomap

/** Main logger class */

class Logger {
    static constexpr size_t QUEUE_CAP = 1024;
    static constexpr size_t MAX_MSG_LEN = 256;

    struct LogEntry {
        uint16_t len;          // length of valid chars in msg[]
        char msg[MAX_MSG_LEN]; // UTF-8 / ASCII payload
    };

    using queue_t = spsc_bounded_queue<LogEntry, QUEUE_CAP>;

public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void shutdown() {
        done_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    ~Logger() {
        shutdown();
    }

    template <typename Id, typename... Args>
    void enqueue(Id id, Args&&... args) {
        auto&& fmt = pseudomap::get(id);
        auto&& s = std::vformat(fmt, std::make_format_args(args...));

        LogEntry e{};
        e.len = static_cast<uint16_t>(std::min(s.size(), MAX_MSG_LEN - 1));
        std::memcpy(e.msg, s.data(), e.len);
        e.msg[e.len] = '\0';

        queue_.enqueue(e);
    }

private:
    Logger()
        : done_{false}, worker_{ [this] { run(); } } 
    {}

    void run() {
        LogEntry e{};
        while (!done_.load(std::memory_order_acquire)) {
            if (queue_.dequeue(e)) {
                std::cout.write(e.msg, e.len);
                std::cout.put('\n');
            } else {
                std::this_thread::yield();
            }
        }
        // flush any remaining entries
        while (queue_.dequeue(e)) {
            std::cout.write(e.msg, e.len);
            std::cout.put('\n');
        }
    }

    queue_t queue_{};
    std::atomic<bool> done_;
    std::thread worker_;
};

} // namespace steroidslog

/** Macros */
#define LOG_DEBUG(fmt, ...)                                                    \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return "[DEBUG] " fmt; };    \
        steroidslog::Logger::instance().enqueue(_log_id __VA_OPT__(, )         \
                                                    __VA_ARGS__);              \
    }

#define LOG_INFO(fmt, ...)                                                     \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return "[INFO] " fmt; };     \
        steroidslog::Logger::instance().enqueue(_log_id __VA_OPT__(, )         \
                                                    __VA_ARGS__);              \
    }

#define LOG_WARN(fmt, ...)                                                     \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return "[WARNING] " fmt; };  \
        steroidslog::Logger::instance().enqueue(_log_id __VA_OPT__(, )         \
                                                    __VA_ARGS__);              \
    }

#define LOG_ERROR(fmt, ...)                                                    \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return "[ERROR] " fmt; };    \
        steroidslog::Logger::instance().enqueue(_log_id __VA_OPT__(, )         \
                                                    __VA_ARGS__);              \
    }
