/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */
 
#pragma once

#include <string_view>

#define ID(x) [x]() constexpr { return x; }

namespace SimpleMap {
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
} // namespace SimpleMap