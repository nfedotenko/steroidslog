/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <steroidslog/steroidslog.h>

using namespace steroidslog;

//------------------------------------------------------------------------------

TEST(Logger, MakeArgslotIntegral) {
    auto a = make_argslot(42);
    bool ok = false;
    std::visit(
        [&](auto v) {
            using V = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_integral_v<V>) {
                ok = (static_cast<long long>(v) == 42);
            }
        },
        a);
    EXPECT_TRUE(ok);
}

TEST(Logger, MakeArgslotFloating) {
    auto a = make_argslot(3.5);
    bool ok = false;
    std::visit(
        [&](auto v) {
            using V = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_floating_point_v<V>) {
                ok = (v > 3.49 && v < 3.51);
            }
        },
        a);
    EXPECT_TRUE(ok);
}

TEST(Logger, MakeArgslotStringView) {
    constexpr const char* lit = "hello";
    auto a = make_argslot(lit);
    bool ok = false;
    std::visit(
        [&](auto v) {
            using V = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string_view>) {
                ok = (v == "hello");
            }
        },
        a);
    EXPECT_TRUE(ok);
}

//------------------------------------------------------------------------------

TEST(Logger, RawRecordArgCountAndCopy) {
    RawLogRecord r{};
    r.fmt_id = 123;
    // 3 args
    auto a0 = make_argslot(7);
    auto a1 = make_argslot(2.5);
    auto a2 = make_argslot("x");
    steroidslog::arg_slot_t temp[3] = {a0, a1, a2};
    r.arg_count = 3;
    std::memcpy(r.args, temp, sizeof(temp));
    // Verify "types" and values
    bool ok_int = false, ok_double = false, ok_sv = false;
    std::visit(
        [&](auto v) {
            if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(v)>>) {
                ok_int = (static_cast<long long>(v) == 7);
            }
        },
        r.args[0]);
    std::visit(
        [&](auto v) {
            if constexpr (std::is_floating_point_v<
                              std::remove_cvref_t<decltype(v)>>) {
                ok_double = (v > 2.49 && v < 2.51);
            }
        },
        r.args[1]);
    std::visit(
        [&](auto v) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(v)>,
                                         std::string_view>) {
                ok_sv = (v == "x");
            }
        },
        r.args[2]);
    EXPECT_EQ(r.arg_count, 3);
    EXPECT_TRUE(ok_int && ok_double && ok_sv);
}

//------------------------------------------------------------------------------

// Utility RAII to capture std::cout
struct CoutCapture {
    CoutCapture() : old_buf(std::cout.rdbuf(capture.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_buf); }
    std::ostringstream capture;

private:
    std::streambuf* old_buf;
};

// Tests for Logger formatting and ordering
TEST(Logger, SingleThreadFormatting) {
    CoutCapture capture;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    STERLOG_INFO("Test {}", 42);
    STERLOG_DEBUG("Hello {}", std::string("world"));
    STERLOG_WARN("Number: {}", 1.234f);
    STERLOG_ERROR("Some big and scary error message...");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto out = capture.capture.str();

    size_t p1 = out.find("[INFO] Test 42");
    size_t p2 = out.find("[DEBUG] Hello world");
    size_t p3 = out.find("[WARNING] Number: 1.234");
    size_t p4 = out.find("[ERROR] Some big and scary error message...");
    EXPECT_NE(p1, std::string::npos);
    EXPECT_NE(p2, std::string::npos);
    EXPECT_NE(p3, std::string::npos);
    EXPECT_NE(p4, std::string::npos);
    EXPECT_LT(p1, p2);
    EXPECT_LT(p2, p3);
    EXPECT_LT(p3, p4);
}

TEST(Logger, MultiThreadInterleaving) {
    CoutCapture capture;
    std::thread t([&] {
        for (int i = 0; i < 5; ++i) {
            STERLOG_DEBUG("T{}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    for (int i = 0; i < 5; ++i) {
        STERLOG_INFO("M{}", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
    }
    t.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto out = capture.capture.str();

    for (int i = 0; i < 5; ++i) {
        EXPECT_NE(out.find("[DEBUG] T" + std::to_string(i)), std::string::npos);
        EXPECT_NE(out.find("[INFO] M" + std::to_string(i)), std::string::npos);
    }
}

// Shutdown does not corrupt messages
TEST(Logger, ShutdownFlushesQueue) {
    CoutCapture capture;
    auto& log = Logger::instance();

    STERLOG_INFO("Before shutdown");
    log.shutdown();

    auto out = capture.capture.str();
    EXPECT_NE(out.find("[INFO] Before shutdown"), std::string::npos);
}
