/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <utility>

/** Inspired by: https://github.com/hogliux/semimap */

namespace steroidslog {

template <typename, typename, bool>
struct default_tag {};

template <typename Key, typename Value,
          typename Tag = default_tag<Key, Value, true>>
class static_map {
public:
    static_map() = delete;

    template <typename... Args>
    static Value& get(const Key& key, Args&&... args) {
        auto it = runtime_map.find(key);

        if (it != runtime_map.end()) {
            return *it->second;
        }

        return *runtime_map.emplace_hint(it, key,
                u_ptr(new Value(std::forward<Args>(args)...),{nullptr}))->second;
    }

private:
    struct value_deleter {
        bool* i_flag = nullptr;

        void operator()(Value* v) {
            if (i_flag != nullptr) {
                v->~Value();
                *i_flag = false;
            } else {
                delete v;
            }
        }
    };

    using u_ptr = std::unique_ptr<Value, value_deleter>;

    template <typename> alignas(Value) static char storage[sizeof(Value)];
    template <typename> static bool init_flag;
    static std::unordered_map<Key, std::unique_ptr<Value, value_deleter>>
        runtime_map;
};

template <typename Key, typename Value, typename Tag>
std::unordered_map<Key, typename static_map<Key, Value, Tag>::u_ptr>
    static_map<Key, Value, Tag>::runtime_map;

template <typename Key, typename Value, typename Tag>
template <typename>
alignas(Value) char static_map<Key, Value, Tag>::storage[sizeof(Value)];

template <typename Key, typename Value, typename Tag>
template <typename>
bool static_map<Key, Value, Tag>::init_flag = false;

//==============================================================================

using pseudomap = static_map<uint32_t, std::string_view>;

constexpr uint32_t fnv1a_32(const char* s, uint32_t v = 2166136261u) noexcept {
    return (*s == '\0')
            ? v
            : fnv1a_32(s + 1, (v ^ static_cast<uint8_t>(*s)) * 16777619u);
}

} // namespace steroidslog
