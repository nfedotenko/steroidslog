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

TEST(Logger, EncodedSizeMatchesLayout) {
    // tag + 8 payload bytes for numbers
    EXPECT_EQ(detail::encoded_size(42), 9u);
    EXPECT_EQ(detail::encoded_size(2.5), 9u);
    // tag + u32 length + inline bytes for strings
    EXPECT_EQ(detail::encoded_size("hello"), 1u + 4u + 5u);
    EXPECT_EQ(detail::encoded_size(std::string(300, 'x')),
              1u + 4u + MAX_STR_ARG); // truncated to the cap
    // tag + pointer + length for static strings
    EXPECT_EQ(detail::encoded_size(static_str{"hello"}), 1u + 8u + 4u);
}

TEST(Logger, EncodeArgRoundtrip) {
    std::byte buf[64];
    detail::cursor c{buf};
    encode_arg(c, 7);
    encode_arg(c, std::string_view{"abc"});

    const std::byte* p = buf;
    EXPECT_EQ(static_cast<ArgTag>(*p), ArgTag::U64);
    uint64_t v = 0;
    std::memcpy(&v, p + 1, sizeof(v));
    EXPECT_EQ(v, 7u);
    p += 9;

    EXPECT_EQ(static_cast<ArgTag>(*p), ArgTag::Str);
    uint32_t len = 0;
    std::memcpy(&len, p + 1, sizeof(len));
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(std::memcmp(p + 5, "abc", 3), 0);
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
    STERLOG_DEBUG("Hello {}", "world");
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

// Strings are copied into the record at the call site: logging a temporary
// and destroying it before the consumer runs must still print the right
// bytes. Run under ASan in CI — the old string_view shipping was a
// use-after-free here.
TEST(Logger, TemporaryStringsAreCopied) {
    CoutCapture capture;

    for (int i = 0; i < 64; ++i) {
        std::string tmp = "tmp-" + std::to_string(i);
        STERLOG_INFO("value={}", tmp);
        // Clobber the freed storage to make stale reads visible even
        // without ASan.
        tmp.assign(tmp.size(), '#');
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto out = capture.capture.str();
    for (int i = 0; i < 64; ++i) {
        const std::string expect = "[INFO] value=tmp-" + std::to_string(i);
        EXPECT_NE(out.find(expect), std::string::npos) << expect;
    }
    EXPECT_EQ(out.find('#'), std::string::npos);
}

TEST(Logger, OversizedStringIsTruncatedNotCorrupted) {
    CoutCapture capture;

    const std::string big(MAX_STR_ARG + 100, 'z');
    STERLOG_INFO("big={}", big);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto out = capture.capture.str();
    // The argument is truncated to MAX_STR_ARG and the line to MAX_MSG_LEN;
    // what survives must be the expected prefix.
    EXPECT_NE(out.find("[INFO] big=zzzz"), std::string::npos);
}

TEST(Logger, StaticStrIsShippedByPointer) {
    CoutCapture capture;

    static constexpr std::string_view kBanner = "static banner";
    STERLOG_INFO("msg={}", static_str{kBanner});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto out = capture.capture.str();
    EXPECT_NE(out.find("[INFO] msg=static banner"), std::string::npos);
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
