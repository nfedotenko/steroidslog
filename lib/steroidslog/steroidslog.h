/*
 * Copyright(c) 2025-2026 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "misc/spsc_byte_ring.h"
#include "misc/tsc_clock.h"

#include <steroids/containers/id_map.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
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

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <immintrin.h>
#define STERLOG_CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define STERLOG_CPU_PAUSE() __asm__ volatile("yield")
#else
#define STERLOG_CPU_PAUSE() ((void)0)
#endif

namespace steroidslog {

// Compile-time format ids come from the family's hash.
using steroids::fnv1a_32;

// Format-string registry: id → pointer to the static format literal.
// steroids::id_map replaced the logger's private pseudomap — same lock-free
// open addressing, wait-free readers, now shared across the family.
inline auto& fmt_registry() {
    static steroids::id_map<const char*, (std::size_t{1} << 16)> instance;
    return instance;
}

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
//   u64 ticks (rdticks() at the call site; wall time is resolved on emit)
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
    F32 = 7,  // floats keep their own width for shortest round-trip output
};

namespace detail {

template <class T>
concept string_like =
    std::is_convertible_v<const T&, std::string_view> &&
    !std::is_same_v<std::remove_cvref_t<T>, static_str>;

// Type used for compile-time std::format_string validation: static_str is
// our own wrapper, the formatter sees it as a string_view.
template <class T>
using fmt_check_t =
    std::conditional_t<std::is_same_v<std::remove_cvref_t<T>, static_str>,
                       std::string_view, std::remove_cvref_t<T>>;

template <class T>
std::size_t encoded_size(const T& v) noexcept {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, static_str>) {
        return 1 + sizeof(uint64_t) + sizeof(uint32_t);
    } else if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, char>) {
        return 1 + 1;
    } else if constexpr (std::is_same_v<U, float>) {
        return 1 + sizeof(float);
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
    } else if constexpr (std::is_same_v<U, float>) {
        c.put_val(static_cast<uint8_t>(ArgTag::F32));
        c.put_val(v);
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

// What a producer does when its ring is full.
enum class OverflowPolicy : uint8_t {
    Drop,  // count the record as dropped and move on (default)
    Block, // spin with backoff until the consumer frees space
};

//------------------------------------------------------------------------------

class Logger final {
    using queue_t = spsc_byte_ring<QUEUE_BYTES>;

    struct ProducerNode {
        alignas(STERLOG_CACHELINE) queue_t q;
        alignas(STERLOG_CACHELINE) std::atomic<bool> active{true};
        ProducerNode* next{nullptr};
        // Written by the producer under the Drop policy.
        alignas(STERLOG_CACHELINE) std::atomic<uint64_t> dropped{0};
        // Consumer-owned: how many drops it has already reported.
        alignas(STERLOG_CACHELINE) uint64_t dropped_seen{0};
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

    // The policy applies to every producer; checked only when a ring is full.
    static void set_overflow_policy(OverflowPolicy p) noexcept {
        policy_.store(p, std::memory_order_relaxed);
    }

    static OverflowPolicy overflow_policy() noexcept {
        return policy_.load(std::memory_order_relaxed);
    }

    // Number of live producer nodes; nodes of exited threads are reclaimed
    // by the consumer once drained.
    std::size_t producer_node_count() const noexcept {
        std::size_t n = 0;
        for (ProducerNode* p = head_.load(std::memory_order_acquire); p;
             p = p->next) {
            ++n;
        }
        return n;
    }

    // Total records dropped across all producers since startup, including
    // producers whose nodes have already been reclaimed.
    uint64_t dropped_count() const noexcept {
        uint64_t total = reclaimed_dropped_.load(std::memory_order_relaxed);
        for (ProducerNode* p = head_.load(std::memory_order_acquire); p;
             p = p->next) {
            total += p->dropped.load(std::memory_order_relaxed);
        }
        return total;
    }

    // Fmt is a consteval lambda returning the format literal; the format
    // string is validated against the argument types at compile time with
    // the same rules as std::format itself.
    template <uint32_t Id, class Fmt, class... Args>
    void enqueue(Fmt, Args&&... args) noexcept {
        static_assert(sizeof...(Args) <= MAX_ARGS, "Too many log args");
        [[maybe_unused]] static constexpr auto checked_fmt_ =
            std::format_string<detail::fmt_check_t<Args>...>(Fmt{}());
        ProducerNode* node = ensure_tls_node();

        const uint64_t ticks = rdticks(); // stamp at the call, not at emit

        constexpr std::size_t header_size = sizeof(uint32_t) +
                                            sizeof(uint32_t) +
                                            sizeof(uint64_t) + sizeof(uint8_t);
        const std::size_t payload =
            (std::size_t{0} + ... + detail::encoded_size(args));
        const std::size_t total = (header_size + payload + 3u) & ~std::size_t{3};

        std::byte* p = node->q.reserve(total);
        if (!p) [[unlikely]] {
            if (overflow_policy() == OverflowPolicy::Drop) {
                node->dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // Block: spin with exponential backoff until the consumer frees
            // space. Falls back to dropping if the logger is shutting down —
            // nobody will ever drain the ring then.
            uint32_t spins = 0;
            do {
                if (done_.load(std::memory_order_acquire)) {
                    node->dropped.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                const uint32_t pauses = 1u << std::min(spins++, 6u);
                for (uint32_t i = 0; i < pauses; ++i) {
                    STERLOG_CPU_PAUSE();
                }
                if (spins > 16) {
                    std::this_thread::yield();
                }
            } while (!(p = node->q.reserve(total)));
        }

        detail::cursor c{p};
        c.put_val(static_cast<uint32_t>(total));
        c.put_val(Id);
        c.put_val(ticks);
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
                // The consumer may reclaim the node from here on. Clear the
                // pointer so a log call from another thread_local's
                // destructor re-registers instead of writing into freed
                // memory (that late node is never reclaimed — acceptable for
                // the exotic log-after-TLS-teardown case).
                node = nullptr;
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
        // Anchor producer tick stamps to wall time. Costs the worker ~10 ms
        // once at startup; producers are unaffected (records queue up and
        // carry their tick stamps taken at the call site).
        const tsc_calibration calib = tsc_calibration::measure();

        // One decoded argument; sv may point into the ring, so a record is
        // always formatted and written before pop().
        struct DecodedArg {
            ArgTag tag{};
            uint64_t u{};
            int64_t i{};
            double d{};
            float f{};
            bool b{};
            char c{};
            std::string_view sv{};
        };

        std::string line;      // reused across records
        line.reserve(512);
        std::string field_fmt; // reused per replacement field
        field_fmt.reserve(32);

        // Formats one decoded argument into `line` with the "{...}" field
        // from the format string, via std::vformat_to — full std::format
        // spec support without per-argument string allocations.
        auto append_arg = [&line](const DecodedArg& a, std::string_view field) {
            auto out = std::back_inserter(line);
            switch (a.tag) {
            case ArgTag::U64:
                std::vformat_to(out, field, std::make_format_args(a.u));
                break;
            case ArgTag::I64:
                std::vformat_to(out, field, std::make_format_args(a.i));
                break;
            case ArgTag::F64:
                std::vformat_to(out, field, std::make_format_args(a.d));
                break;
            case ArgTag::F32:
                std::vformat_to(out, field, std::make_format_args(a.f));
                break;
            case ArgTag::Bool:
                std::vformat_to(out, field, std::make_format_args(a.b));
                break;
            case ArgTag::Char:
                std::vformat_to(out, field, std::make_format_args(a.c));
                break;
            case ArgTag::Str:
            case ArgTag::CStr:
                std::vformat_to(out, field, std::make_format_args(a.sv));
                break;
            }
        };

        // Decodes one record; the string_view args may point into the ring,
        // so the record is consumed (formatted and written) before pop().
        auto format_and_emit = [&](std::span<const std::byte> rec) {
            const std::byte* p = rec.data();
            p += sizeof(uint32_t); // total size, already known from the span

            uint32_t id = 0;
            std::memcpy(&id, p, sizeof(id));
            p += sizeof(id);

            uint64_t ticks = 0;
            std::memcpy(&ticks, p, sizeof(ticks));
            p += sizeof(ticks);

            uint8_t argc = 0;
            std::memcpy(&argc, p, sizeof(argc));
            p += sizeof(argc);

            const char* fmt_cstr = fmt_registry().get(id);
            // Fallback if not registered for some reason
            std::string_view fmt =
                fmt_cstr ? std::string_view{fmt_cstr} : std::string_view{"{}"};

            std::array<DecodedArg, MAX_ARGS> args{};
            const std::size_t n_args =
                std::min<std::size_t>(argc, MAX_ARGS);
            for (std::size_t i = 0; i < n_args; ++i) {
                DecodedArg& a = args[i];
                a.tag = static_cast<ArgTag>(*p);
                p += 1;
                switch (a.tag) {
                case ArgTag::U64:
                    std::memcpy(&a.u, p, sizeof(a.u));
                    p += sizeof(a.u);
                    break;
                case ArgTag::I64:
                    std::memcpy(&a.i, p, sizeof(a.i));
                    p += sizeof(a.i);
                    break;
                case ArgTag::Bool:
                    a.b = (*p != std::byte{0});
                    p += 1;
                    break;
                case ArgTag::Char:
                    a.c = static_cast<char>(*p);
                    p += 1;
                    break;
                case ArgTag::F64:
                    std::memcpy(&a.d, p, sizeof(a.d));
                    p += sizeof(a.d);
                    break;
                case ArgTag::F32:
                    std::memcpy(&a.f, p, sizeof(a.f));
                    p += sizeof(a.f);
                    break;
                case ArgTag::Str: {
                    uint32_t len = 0;
                    std::memcpy(&len, p, sizeof(len));
                    p += sizeof(len);
                    a.sv =
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
                    a.sv = std::string_view(
                        reinterpret_cast<const char*>(ptr), len);
                    break;
                }
                }
            }

            // Render replacement fields one by one: "{}", "{:spec}" and
            // positional "{N...}" are supported; a field that cannot be
            // rendered (index out of range, spec invalid for the shipped
            // type, dynamic width "{:{}}") is kept verbatim.
            line.clear();
            {
                // UTC time-of-day with microseconds, resolved from the
                // producer's tick stamp.
                const auto tp = calib.to_wall(ticks);
                const auto since =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        tp.time_since_epoch())
                        .count();
                const std::time_t secs =
                    std::chrono::system_clock::to_time_t(tp);
                std::tm tm{};
#if defined(_WIN32)
                gmtime_s(&tm, &secs);
#else
                gmtime_r(&secs, &tm);
#endif
                char ts[20];
                std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%06d ",
                              tm.tm_hour, tm.tm_min, tm.tm_sec,
                              static_cast<int>(since % 1000000));
                line.append(ts);
            }
            std::size_t auto_idx = 0;
            for (std::size_t i = 0; i < fmt.size();) {
                const char ch = fmt[i];
                if (ch == '{') {
                    if (i + 1 < fmt.size() && fmt[i + 1] == '{') { // {{
                        line.push_back('{');
                        i += 2;
                        continue;
                    }
                    const std::size_t close = fmt.find('}', i);
                    if (close == std::string_view::npos) { // lone '{'
                        line.append(fmt.substr(i));
                        break;
                    }
                    const std::string_view inner =
                        fmt.substr(i + 1, close - i - 1);
                    // Optional explicit argument index before ':' or '}'.
                    std::size_t k = 0;
                    std::size_t idx = 0;
                    while (k < inner.size() && inner[k] >= '0' &&
                           inner[k] <= '9') {
                        idx = idx * 10 + static_cast<std::size_t>(
                                             inner[k] - '0');
                        ++k;
                    }
                    const bool have_idx =
                        k > 0 && (k == inner.size() || inner[k] == ':');
                    const std::size_t ai = have_idx ? idx : auto_idx++;
                    field_fmt.clear();
                    field_fmt.push_back('{');
                    field_fmt.append(have_idx ? inner.substr(k) : inner);
                    field_fmt.push_back('}');

                    const std::string_view raw = fmt.substr(i, close - i + 1);
                    if (ai >= n_args) {
                        line.append(raw);
                    } else {
                        try {
                            append_arg(args[ai], field_fmt);
                        } catch (const std::exception&) {
                            line.append(raw);
                        }
                    }
                    i = close + 1;
                } else if (ch == '}') { // }} or lone '}' -> literal
                    line.push_back('}');
                    i += (i + 1 < fmt.size() && fmt[i + 1] == '}') ? 2 : 1;
                } else {
                    line.push_back(ch);
                    ++i;
                }
            }

            const std::size_t n =
                std::min<std::size_t>(line.size(), MAX_MSG_LEN - 1);
            std::cout.write(line.data(), static_cast<std::streamsize>(n));
            std::cout.put('\n');
        };

        // Surfaces drops instead of losing them silently: whenever a
        // producer's drop counter moved, say so in the output stream.
        auto report_drops = [](ProducerNode* p) {
            const uint64_t d = p->dropped.load(std::memory_order_relaxed);
            if (d != p->dropped_seen) {
                std::cout << "[WARNING] steroidslog: dropped "
                          << (d - p->dropped_seen) << " records (total " << d
                          << ")\n";
                p->dropped_seen = d;
            }
        };

        while (!done_.load(std::memory_order_acquire)) {
            bool did = false;
            // Poll all queues in a round-robin; batch to amortize fences.
            // Nodes whose thread has exited are drained and reclaimed here:
            // active=false is released by the thread-local destructor after
            // the producer's last ring write, so once the ring is empty the
            // node can never receive data again. Producers only push-front,
            // so the single consumer may safely rewrite next-links.
            ProducerNode* prev = nullptr;
            ProducerNode* p = head_.load(std::memory_order_acquire);
            while (p) {
                ProducerNode* next = p->next;
                report_drops(p);
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
                if (!p->active.load(std::memory_order_acquire) &&
                    p->q.empty()) {
                    if (prev) {
                        prev->next = next;
                        reclaimed_dropped_.fetch_add(
                            p->dropped.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
                        delete p;
                        p = next;
                        continue;
                    }
                    ProducerNode* expected = p;
                    if (head_.compare_exchange_strong(
                            expected, next, std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                        reclaimed_dropped_.fetch_add(
                            p->dropped.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
                        delete p;
                        p = next;
                        continue;
                    }
                    // A producer pushed new nodes in front; p is no longer
                    // the head. Leave it for the next pass.
                }
                prev = p;
                p = next;
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
            report_drops(p);
        }
    }

private:
    // Global registry of producer queues (intrusive lock-free push-front)
    static std::atomic<ProducerNode*> head_;

    // Per-thread registration
    static thread_local TL tls_;

    static std::atomic<OverflowPolicy> policy_;

    // Drops carried over from reclaimed producer nodes.
    static std::atomic<uint64_t> reclaimed_dropped_;

    std::atomic<bool> done_{false};
    std::thread worker_;
};

inline std::atomic<Logger::ProducerNode*> Logger::head_{nullptr};
inline thread_local Logger::TL Logger::tls_{};
inline std::atomic<OverflowPolicy> Logger::policy_{OverflowPolicy::Drop};
inline std::atomic<uint64_t> Logger::reclaimed_dropped_{0};

} // namespace steroidslog

#undef STERLOG_CACHELINE
#undef STERLOG_CPU_PAUSE

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
            [[maybe_unused]] static bool SL_CAT(_reg_, __LINE__) =             \
                fmt_registry().put(SL_CAT(_id_, __LINE__),                     \
                                   SL_LEVEL_PREFIX(level) fmt);                \
            Logger::instance().enqueue<SL_CAT(_id_, __LINE__)>(               \
                []() consteval {                                               \
                    return std::string_view(SL_LEVEL_PREFIX(level) fmt);      \
                } __VA_OPT__(, ) __VA_ARGS__);                                 \
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
