/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "log_levels.h"
#include "misc/pseudomap.h"
#include "misc/small_function.h"
#include "misc/spsc_bounded_queue.h"

#include <atomic>
#include <format>
#include <iostream>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

#ifdef __GNUC__
#include <pthread.h>
#include <sched.h>
#endif

class Logger {
    static constexpr size_t FUNCTION_CAP = 256;
    static constexpr size_t QUEUE_CAP = 1024;
    using small_function = SmallFunction<FUNCTION_CAP>;
    using spsc_queue = spsc_bounded_queue<small_function, QUEUE_CAP>;

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
        small_function task{
            [id,
             tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                auto&& fmt_str = pseudo_map::get(id);
                auto&& s = std::apply(
                    [&](auto&... unpacked) {
                        return std::vformat(fmt_str,
                                            std::make_format_args(unpacked...));
                    },
                    tup);
                std::cout << '[' << to_string(Lvl) << "] " << s << '\n';
            }
        };
        while (!queue_.enqueue(std::move(task))) {
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
        : done_{false}, worker_{[this] { run(); }} 
    {
#ifdef __GNUC__
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(1, &cpus); // pin to CPU #1
        pthread_setaffinity_np(worker_.native_handle(), sizeof(cpus), &cpus);
#endif
    }

    void run() {
        small_function task;
        while (!done_.load(std::memory_order_acquire)) {
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

    spsc_queue queue_{};
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
