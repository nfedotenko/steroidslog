/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace steroidslog {

// constexpr FNV-1a (32-bit) for compile-time ids
constexpr uint32_t fnv1a_32_constexpr(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

template <std::size_t N>
constexpr uint32_t fnv1a_32(const char (&s)[N]) noexcept {
    // strip trailing '\0'
    return fnv1a_32_constexpr(std::string_view{s, N - 1});
}

//------------------------------------------------------------------------------
// Ultra-light lock-free open-addressed table keyed by uint32_t id.
// Single writer per id (via function-local static in macros), many lock-free
// readers. Stores pointer to a static string (format literal); no ownership.
//------------------------------------------------------------------------------
namespace pseudomap_detail {
static constexpr std::size_t CAP = 1u << 16;
static constexpr std::size_t MASK = CAP - 1;

struct Slot {
    std::atomic<uint32_t> key{ 0 }; // 0 = empty, else = id
    std::atomic<const char*> ptr{ nullptr };
};

inline Slot& slot(std::size_t i) {
    static Slot table[CAP];
    return table[i];
}

inline std::size_t index(uint32_t id, std::size_t probe) {
    // Linear probe
    return (static_cast<std::size_t>(id) + probe) & MASK;
}

inline void put(uint32_t id, std::string_view sv) {
    const char* p = sv.data();
    std::size_t probe = 0;
    for (;;) {
        Slot& s = slot(index(id, probe++));
        uint32_t expected = 0;
        if (s.key.compare_exchange_strong(expected, id,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed)) {
            s.ptr.store(p, std::memory_order_release);
            return;
        }
        if (expected == id) {
            // already present; ensure ptr is set
            const char* nullp = nullptr;
            (void)s.ptr.compare_exchange_strong(
                nullp, p, std::memory_order_acq_rel, std::memory_order_relaxed);
            return;
        }
        // else collision; keep probing
    }
}

inline std::string_view get_view(uint32_t id) {
    std::size_t probe = 0;
    for (;;) {
        Slot& s = slot(index(id, probe++));
        const uint32_t k = s.key.load(std::memory_order_acquire);
        if (k == 0) {
            break; // empty slot -> not found
        }
        if (k == id) {
            const char* p = s.ptr.load(std::memory_order_acquire);
            return p ? std::string_view{p} : std::string_view{};
        }
    }
    return {};
}
} // namespace pseudomap_detail

// Returned proxy allows both "assignment to register" and "implicit read".
struct PseudoRef {
    uint32_t id;

    PseudoRef& operator=(std::string_view sv) noexcept {
        pseudomap_detail::put(id, sv);
        return *this;
    }

    explicit operator std::string_view() const noexcept {
        return pseudomap_detail::get_view(id);
    }
};

namespace pseudomap {

// Public API
inline PseudoRef get(uint32_t id) noexcept { return PseudoRef{ id }; }

} // namespace pseudomap

} // namespace steroidslog
