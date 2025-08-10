/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#include "steroidslog/steroidslog.h"

#include <chrono>
#include <sstream>
#include <thread>

using namespace steroidslog;

// Utility RAII to capture std::cout
struct CoutCapture {
    CoutCapture() : old_buf(std::cout.rdbuf(capture.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_buf); }
    std::ostringstream capture;

private:
    std::streambuf* old_buf;
};

// Tests for Logger formatting and ordering
TEST(LoggerTests, SingleThreadFormatting) {
    CoutCapture capture;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    STERLOG_INFO("Test {}", 42);
    STERLOG_DEBUG("Hello {}", std::string("world"));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto out = capture.capture.str();

    size_t p1 = out.find("[INFO] Test 42");
    size_t p2 = out.find("[DEBUG] Hello world");
    EXPECT_NE(p1, std::string::npos);
    EXPECT_NE(p2, std::string::npos);
    EXPECT_LT(p1, p2);
}

TEST(LoggerTests, MultiThreadInterleaving) {
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
TEST(LoggerTests, ShutdownFlushesQueue) {
    CoutCapture capture;
    auto& log = Logger::instance();

    STERLOG_INFO("Before shutdown");
    log.shutdown();

    auto out = capture.capture.str();
    EXPECT_NE(out.find("[INFO] Before shutdown"), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
