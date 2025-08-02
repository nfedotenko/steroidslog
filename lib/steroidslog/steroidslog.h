/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "log_levels.h"
#include "misc/spsc_bounded_queue.h"
#include "misc/pseudomap.h"
#include "misc/small_function.h"

#include <atomic>
#include <format>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

class Logger {
    using small_function = SmallFunction<256>;

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
    void enqueue(LogLevel lvl, Id id, Args&&... args) {
        small_function task{
            [lvl, id,
             tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                auto fmt_str = SimpleMap::get(id);
                auto s = std::apply(
                    [&](auto&... unpacked) {
                        return std::vformat(fmt_str,
                                            std::make_format_args(unpacked...));
                    },
                    tup);
                std::cout << '[' << to_string(lvl) << "] " << s << '\n';
            }};
        while (!queue_.enqueue(std::move(task))) {
            std::this_thread::yield();
        }
    }

    template <typename Id, typename... A>
    void debug(Id id, A&&... a) {
        enqueue(LogLevel::Debug, id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void info(Id id, A&&... a) {
        enqueue(LogLevel::Info, id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void warn(Id id, A&&... a) {
        enqueue(LogLevel::Warning, id, std::forward<A>(a)...);
    }

    template <typename Id, typename... A>
    void error(Id id, A&&... a) {
        enqueue(LogLevel::Error, id, std::forward<A>(a)...);
    }

private:
    Logger() : done_{false}, worker_{[this] { run(); }} {}

    void run() {
        small_function task;
        while (!done_.load(std::memory_order_acquire)) {
            small_function task;
            if (queue_.dequeue(task)) {
                task();
            } else {
                std::this_thread::yield();
            }
        }
        while (queue_.dequeue(task)) {
            task();
        }
    }

    static constexpr size_t QUEUE_CAP = 1024;
    spsc_bounded_queue<small_function, QUEUE_CAP> queue_;
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
