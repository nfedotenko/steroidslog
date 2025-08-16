/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "misc/pseudomap.h"
#include "misc/spsc_bounded_queue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(_MSC_VER)
#define STERLOG_RESTRICT __restrict
#elif defined(__clang__) || defined(__GNUC__)
#define STERLOG_RESTRICT __restrict__
#else
#define STERLOG_RESTRICT
#endif

#define STERLOG_CACHELINE 64

namespace steroidslog {

//------------------------------------------------------------------------------
// Tunables
//------------------------------------------------------------------------------
static constexpr std::size_t MAX_ARGS = 8;
static constexpr std::size_t QUEUE_CAP = 1024;  // per-thread SPSC capacity
static constexpr std::size_t MAX_MSG_LEN = 256; // emit truncation cap
static constexpr int BATCH_DEQ = 64;            // backend batch per queue

//------------------------------------------------------------------------------
// Arg packing
//------------------------------------------------------------------------------
using arg_slot_t = std::variant<uint64_t, double, std::string_view>;

template <class T>
constexpr arg_slot_t make_argslot(T&& v) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_integral_v<U>) {
        return static_cast<uint64_t>(static_cast<uintptr_t>(v));
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<double>(v);
    } else if constexpr (std::is_convertible_v<U, std::string_view>) {
        return std::string_view(v);
    } else {
        static_assert(!sizeof(U), "Unsupported type for log argument");
    }
}

struct RawLogRecord final {
    uint32_t fmt_id;   // compile-time hash
    uint8_t arg_count; // <= MAX_ARGS
    arg_slot_t args[MAX_ARGS];
};

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warning,
    Error,
    Unknown
};

//------------------------------------------------------------------------------

class Logger final {
    using queue_t = spsc_bounded_queue<RawLogRecord, QUEUE_CAP>;

    struct ProducerNode {
        alignas(STERLOG_CACHELINE) queue_t q;
        alignas(STERLOG_CACHELINE) std::atomic<bool> active{true};
        ProducerNode* next{nullptr};
    };

public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    ~Logger() { shutdown(); }

    void shutdown() {
        done_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    template <uint32_t Id, class... Args>
    void enqueue(Args&&... args) noexcept {
        static_assert(sizeof...(Args) <= MAX_ARGS, "Too many log args");
        ProducerNode* node = ensure_tls_node();

        RawLogRecord rec{Id, static_cast<uint8_t>(sizeof...(Args)), {}};

        if constexpr (sizeof...(Args) > 0) {
            // arg_slot_t tmp[] = {make_argslot(std::forward<Args>(args))...};
            // std::memcpy(rec.args, tmp, sizeof(tmp));
            arg_slot_t* STERLOG_RESTRICT dst = rec.args;
            ((void)(*dst++ = make_argslot(std::forward<Args>(args))), ...);
        }

        // Non-blocking try; drop if full to avoid stalling producers.
        for (int tries = 0; tries < 4; ++tries) {
            if (node->q.enqueue(std::move(rec))) {
                return;
            }
        }
    }

private:
    Logger() : worker_{[this] { run(); }} {}

    static ProducerNode* register_node_() {
        auto* n = new ProducerNode();
        // Push-front intrusive list
        ProducerNode* old = head_.load(std::memory_order_relaxed);
        do {
            n->next = old;
        } while (!head_.compare_exchange_weak(old, n, std::memory_order_release,
                                              std::memory_order_relaxed));
        return n;
    }

    // Thread-local
    struct TL {
        ProducerNode* node{nullptr};

        ~TL() {
            if (node) {
                node->active.store(false, std::memory_order_release);
            }
        }
    };

    ProducerNode* ensure_tls_node() {
        if (!tls_.node) {
            tls_.node = register_node_();
        }
        return tls_.node;
    }

    void run() {
        auto format_simple = [](std::string_view fmt,
                                std::span<std::string_view> args) {
            std::string out;
            out.reserve(fmt.size() + 32);
            std::size_t ai = 0;

            for (std::size_t i = 0; i < fmt.size();) {
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
            std::string_view fmt(pseudomap::get(rec.fmt_id));
            // Fallback if not registered for some reason
            if (fmt.empty()) {
                fmt = std::string_view{"{}"};
            }

            std::array<std::string, MAX_ARGS> storage;
            std::array<std::string_view, MAX_ARGS> views;
            for (std::size_t i = 0; i < rec.arg_count; ++i) {
                std::visit(
                    [&](auto&& val) {
                        using V = std::remove_cvref_t<decltype(val)>;
                        if constexpr (std::is_same_v<V, uint64_t>) {
                            storage[i] = std::to_string(val);
                        } else if constexpr (std::is_same_v<V, double>) {
                            storage[i] = std::to_string(val);
                        } else { // string_view
                            storage[i] = std::string(val);
                        }
                        views[i] = storage[i];
                    },
                    rec.args[i]);
            }

            std::string s = format_simple(fmt,
                std::span<std::string_view>{views.data(), rec.arg_count});
            const std::size_t n =
                std::min<std::size_t>(s.size(), MAX_MSG_LEN - 1);
            std::cout.write(s.data(), static_cast<std::streamsize>(n));
            std::cout.put('\n');
        };

        RawLogRecord rec;
        while (!done_.load(std::memory_order_acquire)) {
            bool did = false;
            // Poll all queues in a round-robin; batch to amortize fences
            for (ProducerNode* p = head_.load(std::memory_order_acquire); p;
                 p = p->next) {
                if (!p->active.load(std::memory_order_acquire) &&
                    p->q.approx_size() == 0) {
                    continue;
                }
                int k = 0;
                while (k < BATCH_DEQ && p->q.dequeue(rec)) {
                    format_and_emit(rec);
                    ++k;
                    did = true;
                }
            }
            if (!did) {
                std::this_thread::yield();
            }
        }

        // Drain
        for (auto* p = head_.load(std::memory_order_acquire); 
            p; p = p->next) {
            while (p->q.dequeue(rec)) {
                format_and_emit(rec);
            }
        }
    }

private:
    // Global registry of producer queues (intrusive lock-free push-front)
    static std::atomic<ProducerNode*> head_;

    // Per-thread registration
    static thread_local TL tls_;

    std::atomic<bool> done_{false};
    std::thread worker_;
};

inline std::atomic<Logger::ProducerNode*> Logger::head_{nullptr};
inline thread_local Logger::TL Logger::tls_{};

} // namespace steroidslog

#undef STERLOG_RESTRICT
#undef STERLOG_CACHELINE

//------------------------------------------------------------------------------

#ifndef SL_CAT
#define SL_CAT_IMPL(a, b) a##b
#define SL_CAT(a, b) SL_CAT_IMPL(a, b)
#endif

#define SL_LEVEL_PREFIX_Debug   "[DEBUG] "
#define SL_LEVEL_PREFIX_Info    "[INFO] "
#define SL_LEVEL_PREFIX_Warning "[WARNING] "
#define SL_LEVEL_PREFIX_Error   "[ERROR] "
#define SL_LEVEL_PREFIX(level) SL_CAT(SL_LEVEL_PREFIX_, level)

#ifndef STEROIDSLOG_MIN_LEVEL
#define STEROIDSLOG_MIN_LEVEL Debug
#endif

#define STERLOG(level, fmt, ...)                                               \
    {                                                                          \
        using namespace steroidslog;                                           \
        if constexpr (LogLevel::level >= LogLevel::STEROIDSLOG_MIN_LEVEL) {    \
            constexpr uint32_t SL_CAT(_id_, __LINE__) =                        \
                fnv1a_32(SL_LEVEL_PREFIX(level) fmt);                          \
            [[maybe_unused]] static bool SL_CAT(_reg_, __LINE__) = [] {        \
                pseudomap::get(SL_CAT(_id_, __LINE__)) =                       \
                    std::string_view(SL_LEVEL_PREFIX(level) fmt);              \
                return true;                                                   \
            }();                                                               \
            Logger::instance().enqueue<SL_CAT(_id_, __LINE__)>(__VA_ARGS__);   \
        }                                                                      \
    }

#define STERLOG_DEBUG(fmt, ...)                                                \
    STERLOG(Debug, fmt __VA_OPT__(, ) __VA_ARGS__)
#define STERLOG_INFO(fmt, ...)                                                 \
    STERLOG(Info, fmt __VA_OPT__(, ) __VA_ARGS__)
#define STERLOG_WARN(fmt, ...)                                                 \
    STERLOG(Warning, fmt __VA_OPT__(, ) __VA_ARGS__)
#define STERLOG_ERROR(fmt, ...)                                                \
    STERLOG(Error, fmt __VA_OPT__(, ) __VA_ARGS__)
