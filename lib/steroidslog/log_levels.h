/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <cstdint>

enum class LogLevel : uint8_t { Debug, Info, Warning, Error, Unknown };

consteval const char* to_string(LogLevel lvl) {
    if (lvl == LogLevel::Debug) {
        return "DEBUG";
    } else if (lvl == LogLevel::Info) {
        return "INFO";
    } else if (lvl == LogLevel::Warning) {
        return "WARNING";
    } else if (lvl == LogLevel::Error) {
        return "ERROR";
    } else {
        return "UNKNOWN";
    }
}
