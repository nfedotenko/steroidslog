/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <limits>
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
    // tag + 1 payload byte for bool and char
    EXPECT_EQ(detail::encoded_size(true), 2u);
    EXPECT_EQ(detail::encoded_size('x'), 2u);
    // floats keep their own 4-byte width
    EXPECT_EQ(detail::encoded_size(1.5f), 5u);
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
    EXPECT_EQ(static_cast<ArgTag>(*p), ArgTag::I64); // int is signed
    int64_t v = 0;
    std::memcpy(&v, p + 1, sizeof(v));
    EXPECT_EQ(v, 7);
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

// Signed values must come out with their sign, not as 2^64-style garbage;
// bool and char must render as true/false and the character itself.
TEST(Logger, IntegerSignednessAndCharTypes) {
    CoutCapture capture;

    STERLOG_INFO("neg={}", -1);
    STERLOG_INFO("min={}", INT64_MIN);
    STERLOG_INFO("umax={}", UINT64_MAX);
    STERLOG_INFO("flag={} flag2={}", true, false);
    STERLOG_INFO("ch={}", 'x');
    STERLOG_INFO("short={}", static_cast<short>(-42));
    STERLOG_INFO("uchar={}", static_cast<unsigned char>(200));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto out = capture.capture.str();
    EXPECT_NE(out.find("[INFO] neg=-1"), std::string::npos);
    EXPECT_NE(out.find("[INFO] min=-9223372036854775808"), std::string::npos);
    EXPECT_NE(out.find("[INFO] umax=18446744073709551615"), std::string::npos);
    EXPECT_NE(out.find("[INFO] flag=true flag2=false"), std::string::npos);
    EXPECT_NE(out.find("[INFO] ch=x"), std::string::npos);
    EXPECT_NE(out.find("[INFO] short=-42"), std::string::npos);
    EXPECT_NE(out.find("[INFO] uchar=200"), std::string::npos);
}

// Doubles render in shortest round-trip form via to_chars: tiny values do
// not collapse to 0.000000, specials print as nan/inf.
TEST(Logger, FloatingPointRendering) {
    CoutCapture capture;

    STERLOG_INFO("tiny={}", 1e-8);
    STERLOG_INFO("tenth={}", 0.1);
    STERLOG_INFO("big={}", 123456.789);
    STERLOG_INFO("nan={}", std::numeric_limits<double>::quiet_NaN());
    STERLOG_INFO("pinf={}", std::numeric_limits<double>::infinity());
    STERLOG_INFO("ninf={}", -std::numeric_limits<double>::infinity());
    STERLOG_INFO("f32={}", 1.234f);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto out = capture.capture.str();
    EXPECT_NE(out.find("[INFO] tiny=1e-08"), std::string::npos);
    EXPECT_NE(out.find("[INFO] tenth=0.1\n"), std::string::npos);
    EXPECT_NE(out.find("[INFO] big=123456.789\n"), std::string::npos);
    EXPECT_NE(out.find("[INFO] nan=nan"), std::string::npos);
    EXPECT_NE(out.find("[INFO] pinf=inf"), std::string::npos);
    EXPECT_NE(out.find("[INFO] ninf=-inf"), std::string::npos);
    EXPECT_NE(out.find("[INFO] f32=1.234\n"), std::string::npos);
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

// Under the default Drop policy a flooded ring counts its losses and the
// consumer reports them in the stream instead of losing records silently.
TEST(Logger, DropPolicyCountsAndReports) {
    CoutCapture capture;
    auto& log = Logger::instance();
    ASSERT_EQ(Logger::overflow_policy(), OverflowPolicy::Drop);

    const uint64_t before = log.dropped_count();
    for (int i = 0; i < 200000; ++i) {
        STERLOG_INFO("flood {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_GT(log.dropped_count(), before);
    EXPECT_NE(capture.capture.str().find("steroidslog: dropped"),
              std::string::npos);
}

// Under Block the producer waits for the consumer: every record arrives and
// the drop counter does not move.
TEST(Logger, BlockPolicyLosesNothing) {
    CoutCapture capture;
    auto& log = Logger::instance();
    Logger::set_overflow_policy(OverflowPolicy::Block);

    const uint64_t before = log.dropped_count();
    constexpr int N = 20000;
    for (int i = 0; i < N; ++i) {
        STERLOG_INFO("blk#{}#", i);
    }

    // Wait until the consumer has drained everything (bounded).
    int lines = 0;
    for (int tries = 0; tries < 60; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto out = capture.capture.str();
        lines = 0;
        for (std::size_t pos = 0;
             (pos = out.find("[INFO] blk#", pos)) != std::string::npos;
             ++pos) {
            ++lines;
        }
        if (lines == N) {
            break;
        }
    }
    Logger::set_overflow_policy(OverflowPolicy::Drop);

    EXPECT_EQ(lines, N);
    EXPECT_EQ(log.dropped_count(), before);
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
