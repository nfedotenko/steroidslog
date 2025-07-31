/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "log_levels.h"
#include "misc/mpsc_bounded_queue.h"
#include "misc/pseudomap.h"
#include "misc/small_function.h"

#include <atomic>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    ~Logger() {
        done_.store(true, std::memory_order_release);
        worker_.join();
    }

    template <typename... Args>
    void enqueue(LogLevel lvl, std::string_view fmt, Args&&... args) {
        auto id = ID(fmt);
        auto task =
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
            };
        while (!queue_.try_push(std::move(task))) {
            std::this_thread::yield();
        }
    }

    template <typename... A> void debug(std::string_view fmt, A&&... a) {
        enqueue(LogLevel::Debug, (fmt), std::forward<A>(a)...);
    }

    template <typename... A> void info(std::string_view fmt, A&&... a) {
        enqueue(LogLevel::Info, (fmt), std::forward<A>(a)...);
    }

    template <typename... A> void warn(std::string_view fmt, A&&... a) {
        enqueue(LogLevel::Warning, (fmt), std::forward<A>(a)...);
    }

    template <typename... A> void error(std::string_view fmt, A&&... a) {
        enqueue(LogLevel::Error, (fmt), std::forward<A>(a)...);
    }

private:
    Logger() : done_{false}, worker_{[this] { run(); }} {}

    void run() {
        while (!done_.load(std::memory_order_acquire)) {
            std::function<void()> task;
            if (queue_.try_pop(task)) {
                task();
            } else {
                std::this_thread::yield();
            }
        }
        std::function<void()> task;
        while (queue_.try_pop(task)) {
            task();
        }
    }

    static constexpr size_t QUEUE_CAP = 1024;
    mpsc_bounded_queue<std::function<void()>, QUEUE_CAP> queue_;
    std::atomic<bool> done_;
    std::thread worker_;
};

#define LOG_DEBUG(fmt, ...)                                                    \
    {                                                                          \
        Logger::instance().debug(fmt __VA_OPT__(, ) __VA_ARGS__);              \
    }

#define LOG_INFO(fmt, ...)                                                     \
    {                                                                          \
        Logger::instance().info(fmt __VA_OPT__(, ) __VA_ARGS__);               \
    }

#define LOG_WARN(fmt, ...)                                                     \
    {                                                                          \
        Logger::instance().warn(fmt __VA_OPT__(, ) __VA_ARGS__);               \
    }

#define LOG_ERROR(fmt, ...)                                                    \
    {                                                                          \
        Logger::instance().error(fmt __VA_OPT__(, ) __VA_ARGS__);              \
    }
