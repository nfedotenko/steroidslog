/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "misc/pseudomap.h"
#include "misc/spsc_byte_ring.h"

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

#define STERLOG_CACHELINE 64

namespace steroidslog {

//------------------------------------------------------------------------------
// Tunables
//------------------------------------------------------------------------------
static constexpr std::size_t MAX_ARGS = 8;
static constexpr std::size_t QUEUE_BYTES = 64 * 1024; // per-thread ring size
static constexpr std::size_t MAX_MSG_LEN = 256;       // emit truncation cap
static constexpr std::size_t MAX_STR_ARG = 256;       // inline string arg cap
static constexpr int BATCH_DEQ = 64;                  // backend batch per queue

//------------------------------------------------------------------------------
// Argument encoding
//
// A log record is a variable-length byte sequence in the per-thread ring:
//
//   u32 total_size (multiple of 4, includes this field)
//   u32 fmt_id
//   u8  arg_count
//   per argument: u8 tag, then its payload
//
// Numbers ship as 8 raw bytes. Strings are copied inline (length-prefixed,
// truncated to MAX_STR_ARG) so the record never refers to producer memory —
// logging a temporary std::string is safe. A string wrapped in static_str
// ships as pointer + length instead and must outlive the logger.
//------------------------------------------------------------------------------

// Explicit marker for strings with static storage duration ("foo", string
// constants): the logger ships only the pointer instead of copying the bytes.
struct static_str {
    std::string_view view;
    constexpr explicit static_str(std::string_view sv) noexcept : view(sv) {}
};

enum class ArgTag : uint8_t {
    U64 = 0,
    F64 = 1,
    Str = 2,  // inline bytes: u32 length + data
    CStr = 3, // static storage: u64 pointer + u32 length
    I64 = 4,  // signed integers, rendered with their sign
    Bool = 5, // rendered as true/false, 1 payload byte
    Char = 6, // rendered as the character itself, 1 payload byte
};

namespace detail {

template <class T>
concept string_like =
    std::is_convertible_v<const T&, std::string_view> &&
    !std::is_same_v<std::remove_cvref_t<T>, static_str>;

template <class T>
std::size_t encoded_size(const T& v) noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, static_str>) {
        return 1 + sizeof(uint64_t) + sizeof(uint32_t);
    } else if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, char>) {
        return 1 + 1;
    } else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
        return 1 + sizeof(uint64_t);
    } else if constexpr (string_like<U>) {
        const std::string_view sv(v);
        return 1 + sizeof(uint32_t) + std::min(sv.size(), MAX_STR_ARG);
    } else {
        static_assert(!sizeof(U), "Unsupported type for log argument");
    }
}

struct cursor {
    std::byte* p;

    void put(const void* src, std::size_t n) noexcept {
        std::memcpy(p, src, n);
        p += n;
    }

    template <class T>
    void put_val(T v) noexcept {
        put(&v, sizeof(v));
    }
};

template <class T>
void encode_arg(cursor& c, const T& v) noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, static_str>) {
        c.put_val(static_cast<uint8_t>(ArgTag::CStr));
        c.put_val(reinterpret_cast<uint64_t>(v.view.data()));
        c.put_val(static_cast<uint32_t>(v.view.size()));
    } else if constexpr (std::is_same_v<U, bool>) {
        c.put_val(static_cast<uint8_t>(ArgTag::Bool));
        c.put_val(static_cast<uint8_t>(v ? 1 : 0));
    } else if constexpr (std::is_same_v<U, char>) {
        c.put_val(static_cast<uint8_t>(ArgTag::Char));
        c.put_val(v);
    } else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>) {
        c.put_val(static_cast<uint8_t>(ArgTag::I64));
        c.put_val(static_cast<int64_t>(v));
    } else if constexpr (std::is_integral_v<U>) {
        c.put_val(static_cast<uint8_t>(ArgTag::U64));
        c.put_val(static_cast<uint64_t>(v));
    } else if constexpr (std::is_floating_point_v<U>) {
        c.put_val(static_cast<uint8_t>(ArgTag::F64));
        c.put_val(static_cast<double>(v));
    } else { // string-like: copy the bytes into the record
        const std::string_view sv(v);
        const auto len =
            static_cast<uint32_t>(std::min(sv.size(), MAX_STR_ARG));
        c.put_val(static_cast<uint8_t>(ArgTag::Str));
        c.put_val(len);
        c.put(sv.data(), len);
    }
}

} // namespace detail

enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warning,
    Error,
    Unknown
};

//------------------------------------------------------------------------------

class Logger final {
    using queue_t = spsc_byte_ring<QUEUE_BYTES>;

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

        constexpr std::size_t header_size =
            sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
        const std::size_t payload =
            (std::size_t{0} + ... + detail::encoded_size(args));
        const std::size_t total = (header_size + payload + 3u) & ~std::size_t{3};

        // Non-blocking try; drop if full to avoid stalling producers.
        // (A configurable overflow policy lands in a follow-up.)
        std::byte* p = nullptr;
        for (int tries = 0; tries < 4 && !p; ++tries) {
            p = node->q.reserve(total);
        }
        if (!p) {
            return;
        }

        detail::cursor c{p};
        c.put_val(static_cast<uint32_t>(total));
        c.put_val(Id);
        c.put_val(static_cast<uint8_t>(sizeof...(Args)));
        (detail::encode_arg(c, args), ...);
        node->q.commit();
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

        // Decodes one record; the string_view args may point into the ring,
        // so the record is consumed (formatted and written) before pop().
        auto format_and_emit = [&](std::span<const std::byte> rec) {
            const std::byte* p = rec.data();
            p += sizeof(uint32_t); // total size, already known from the span

            uint32_t id = 0;
            std::memcpy(&id, p, sizeof(id));
            p += sizeof(id);

            uint8_t argc = 0;
            std::memcpy(&argc, p, sizeof(argc));
            p += sizeof(argc);

            std::string_view fmt(pseudomap::get(id));
            // Fallback if not registered for some reason
            if (fmt.empty()) {
                fmt = std::string_view{"{}"};
            }

            std::array<std::string, MAX_ARGS> storage;
            std::array<std::string_view, MAX_ARGS> views;
            const std::size_t n_args =
                std::min<std::size_t>(argc, MAX_ARGS);
            for (std::size_t i = 0; i < n_args; ++i) {
                const auto tag = static_cast<ArgTag>(*p);
                p += 1;
                switch (tag) {
                case ArgTag::U64: {
                    uint64_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    p += sizeof(v);
                    storage[i] = std::to_string(v);
                    views[i] = storage[i];
                    break;
                }
                case ArgTag::I64: {
                    int64_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    p += sizeof(v);
                    storage[i] = std::to_string(v);
                    views[i] = storage[i];
                    break;
                }
                case ArgTag::Bool: {
                    const bool v = (*p != std::byte{0});
                    p += 1;
                    views[i] = v ? std::string_view{"true"}
                                 : std::string_view{"false"};
                    break;
                }
                case ArgTag::Char: {
                    storage[i].assign(reinterpret_cast<const char*>(p), 1);
                    p += 1;
                    views[i] = storage[i];
                    break;
                }
                case ArgTag::F64: {
                    double v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    p += sizeof(v);
                    storage[i] = std::to_string(v);
                    views[i] = storage[i];
                    break;
                }
                case ArgTag::Str: {
                    uint32_t len = 0;
                    std::memcpy(&len, p, sizeof(len));
                    p += sizeof(len);
                    views[i] =
                        std::string_view(reinterpret_cast<const char*>(p), len);
                    p += len;
                    break;
                }
                case ArgTag::CStr: {
                    uint64_t ptr = 0;
                    uint32_t len = 0;
                    std::memcpy(&ptr, p, sizeof(ptr));
                    p += sizeof(ptr);
                    std::memcpy(&len, p, sizeof(len));
                    p += sizeof(len);
                    views[i] = std::string_view(
                        reinterpret_cast<const char*>(ptr), len);
                    break;
                }
                }
            }

            std::string s = format_simple(fmt,
                std::span<std::string_view>{views.data(), n_args});
            const std::size_t n =
                std::min<std::size_t>(s.size(), MAX_MSG_LEN - 1);
            std::cout.write(s.data(), static_cast<std::streamsize>(n));
            std::cout.put('\n');
        };

        while (!done_.load(std::memory_order_acquire)) {
            bool did = false;
            // Poll all queues in a round-robin; batch to amortize fences
            for (ProducerNode* p = head_.load(std::memory_order_acquire); p;
                 p = p->next) {
                if (!p->active.load(std::memory_order_acquire) &&
                    p->q.empty()) {
                    continue;
                }
                int k = 0;
                while (k < BATCH_DEQ) {
                    const auto rec = p->q.front();
                    if (rec.empty()) {
                        break;
                    }
                    format_and_emit(rec);
                    p->q.pop();
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
            for (;;) {
                const auto rec = p->q.front();
                if (rec.empty()) {
                    break;
                }
                format_and_emit(rec);
                p->q.pop();
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
