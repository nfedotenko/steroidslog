/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "misc/pseudomap.h"
#include "misc/spsc_bounded_queue.h"

#include <atomic>
#include <cstring>
#include <format>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace steroidslog {

static constexpr size_t MAX_ARGS = 8;
static constexpr size_t QUEUE_CAP = 1024;
static constexpr size_t MAX_MSG_LEN = 256;

using arg_slot_t = std::variant<uint64_t, double, std::string_view>;

template <typename T>
constexpr arg_slot_t make_argslot(T&& v) {
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_integral_v<U> || std::is_pointer_v<U>) {
        return static_cast<uint64_t>(v);
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<double>(v);
    } else if constexpr (std::is_convertible_v<U, std::string_view>) {
        return std::string_view(v);
    } else {
        static_assert(false, "Unsupported type for log argument");
    }
}

struct RawLogRecord {
    uint32_t   fmt_id;    // compile-time hash
    uint8_t    arg_count; // not greater than MAX_ARGS
    arg_slot_t args[MAX_ARGS];
};

//==============================================================================

class Logger {
    using queue_t = spsc_bounded_queue<RawLogRecord, QUEUE_CAP>;

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

    template <uint32_t Id, typename... Args>
    void enqueue(Args&&... args) {
        static_assert(sizeof...(Args) <= MAX_ARGS, "Too many log args");
        RawLogRecord rec{ Id, static_cast<uint8_t>(sizeof...(Args)), {} };
        arg_slot_t tmp[] = { make_argslot(std::forward<Args>(args))... };
        std::memcpy(rec.args, tmp, sizeof(tmp));
        queue_.enqueue(rec);
    }

private:
    Logger()
        : done_{false}, worker_{ [this] { run(); } } 
    {}

    void run() {
        auto format_simple =
            [](std::string_view fmt,
               std::span<const std::string_view> args) {
            std::string out;
            out.reserve(fmt.size() + 32);
            size_t ai = 0;
            for (size_t i = 0; i < fmt.size();) {
                char c = fmt[i];
                if (c == '{') {
                    if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // escaped {{
                        out.push_back('{');
                        i += 2;
                    } else if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // {}
                        if (ai < args.size())
                            out.append(args[ai++]);
                        else
                            out.append("{}"); // graceful fallback
                        i += 2;
                    } else { // lone '{' -> literal
                        out.push_back('{');
                        ++i;
                    }
                } else if (c == '}') {
                    if (i + 1 < fmt.size() && fmt[i + 1] == '}') { // escaped }}
                        out.push_back('}');
                        i += 2;
                    } else { // lone '}' -> literal
                        out.push_back('}');
                        ++i;
                    }
                } else {
                    out.push_back(c);
                    ++i;
                }
            }
            return out;
        };

        auto format_and_emit = [&](const RawLogRecord& rec) {
            auto&& fmt = pseudomap::get(rec.fmt_id);

            std::array<std::string, MAX_ARGS> storage{};
            std::array<std::string_view, MAX_ARGS> views{};
            for (size_t i = 0; i < rec.arg_count; ++i) {
                std::visit(
                    [&](auto&& val) {
                        using V = std::remove_cvref_t<decltype(val)>;
                        if constexpr (std::is_same_v<V, uint64_t>) {
                            storage[i] = std::to_string(val);
                        } else if constexpr (std::is_same_v<V, double>) {
                            storage[i] = std::to_string(val);
                        } else { // std::string_view
                            storage[i] = std::string(val);
                        }
                        views[i] = storage[i];
                    },
                    rec.args[i]);
            }

            auto&& s = format_simple(fmt,
                std::span<const std::string_view>(views.data(), rec.arg_count));

            const auto n = std::min(s.size(), size_t(MAX_MSG_LEN - 1));
            std::cout.write(s.data(), static_cast<std::streamsize>(n));
            std::cout.put('\n');
        };

        RawLogRecord rec;

        while (!done_.load(std::memory_order_acquire)) {
            if (queue_.dequeue(rec)) {
                format_and_emit(rec);
            } else {
                std::this_thread::yield();
            }
        }
        // Drain on shutdown
        while (queue_.dequeue(rec)) {
            format_and_emit(rec);
        }
    }

    queue_t queue_{};
    std::atomic<bool> done_;
    std::thread worker_;
};

} // namespace steroidslog

//==============================================================================

#define LOG_DEBUG(fmt, ...)                                                    \
    {                                                                          \
        using namespace steroidslog;                                           \
        constexpr uint32_t _id = fnv1a_32("[DEBUG] " fmt);                     \
        [[maybe_unused]] static bool _reg = [_id] {                            \
            pseudomap::get(_id) = std::string_view("[DEBUG] " fmt);            \
            return true;                                                       \
        }();                                                                   \
        Logger::instance().enqueue<_id>(__VA_ARGS__);                          \
    }

#define LOG_INFO(fmt, ...)                                                     \
    {                                                                          \
        using namespace steroidslog;                                           \
        constexpr uint32_t _id = fnv1a_32("[INFO] " fmt);                      \
        [[maybe_unused]] static bool _reg = [_id] {                            \
            pseudomap::get(_id) = std::string_view("[INFO] " fmt);             \
            return true;                                                       \
        }();                                                                   \
        Logger::instance().enqueue<_id>(__VA_ARGS__);                          \
    }

#define LOG_WARN(fmt, ...)                                                     \
    {                                                                          \
        using namespace steroidslog;                                           \
        constexpr uint32_t _id = fnv1a_32("[WARNING] " fmt);                   \
        [[maybe_unused]] static bool _reg = [_id] {                            \
            pseudomap::get(_id) = std::string_view("[WARNING] " fmt);          \
            return true;                                                       \
        }();                                                                   \
        Logger::instance().enqueue<_id>(__VA_ARGS__);                          \
    }

#define LOG_ERROR(fmt, ...)                                                    \
    {                                                                          \
        using namespace steroidslog;                                           \
        constexpr uint32_t _id = fnv1a_32("[ERROR] " fmt);                     \
        [[maybe_unused]] static bool _reg = [_id] {                            \
            pseudomap::get(_id) = std::string_view("[ERROR] " fmt);            \
            return true;                                                       \
        }();                                                                   \
        Logger::instance().enqueue<_id>(__VA_ARGS__);                          \
    }
