/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "log_levels.h"
#include "misc/pseudomap.h"
#include "misc/spsc_bounded_queue.h"

#include <atomic>
#include <cstring>
#include <format>
#include <iostream>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

class Logger {
    static constexpr size_t QUEUE_CAP = 1024;
    static constexpr size_t MAX_MSG_LEN = 256;

    struct LogEntry {
        LogLevel level;
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

    template <LogLevel Lvl, typename Id, typename... Args>
    void enqueue(Id id, Args&&... args) {
        auto&& fmt = pseudo_map::get(id);
        auto&& s = std::vformat(fmt, std::make_format_args(args...));

        LogEntry e{};
        e.level = Lvl;
        e.len = static_cast<uint16_t>(std::min(s.size(), MAX_MSG_LEN - 1));
        std::memcpy(e.msg, s.data(), e.len);
        e.msg[e.len] = '\0';

        while (!queue_.enqueue(e)) {
            std::this_thread::yield();
        }
    }

    template <typename Id, typename... A>
    void debug(Id id, A&&... a) {
        enqueue<LogLevel::Debug>(id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void info(Id id, A&&... a) {
        enqueue<LogLevel::Info>(id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void warn(Id id, A&&... a) {
        enqueue<LogLevel::Warning>(id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void error(Id id, A&&... a) {
        enqueue<LogLevel::Error>(id, std::forward<A>(a)...);
    }

private:
    Logger()
        : done_{false}, worker_{ [this] { run(); } } 
    {}

    void run() {
        LogEntry e{};
        while (!done_.load(std::memory_order_acquire)) {
            if (queue_.dequeue(e)) {
                std::cout.write("[", 1);
                const auto* lvl = to_string(e.level);
                std::cout.write(lvl, std::char_traits<char>::length(lvl));
                std::cout.write("] ", 2);
                std::cout.write(e.msg, e.len);
                std::cout.put('\n');
            } else {
                std::this_thread::yield();
            }
        }
        // flush any remaining entries
        while (queue_.dequeue(e)) {
            std::cout.write("[", 1);
            const auto* lvl = to_string(e.level);
            std::cout.write(lvl, std::char_traits<char>::length(lvl));
            std::cout.write("] ", 2);
            std::cout.write(e.msg, e.len);
            std::cout.put('\n');
        }
    }

    queue_t queue_{};
    std::atomic<bool> done_;
    std::thread worker_;
};

#define LOG_DEBUG(fmt, ...)                                                    \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return fmt; };               \
        Logger::instance().debug(_log_id __VA_OPT__(, ) __VA_ARGS__);          \
    }

#define LOG_INFO(fmt, ...)                                                     \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return fmt; };               \
        Logger::instance().info(_log_id __VA_OPT__(, ) __VA_ARGS__);           \
    }

#define LOG_WARN(fmt, ...)                                                     \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return fmt; };               \
        Logger::instance().warn(_log_id __VA_OPT__(, ) __VA_ARGS__);           \
    }

#define LOG_ERROR(fmt, ...)                                                    \
    {                                                                          \
        constexpr auto _log_id = []() constexpr { return fmt; };               \
        Logger::instance().error(_log_id __VA_OPT__(, ) __VA_ARGS__);          \
    }
