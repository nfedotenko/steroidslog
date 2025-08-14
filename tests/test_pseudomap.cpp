/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#if __has_include(<misc/pseudomap.h>)
#include <misc/pseudomap.h>
#define STEROIDSLOG_HAVE_PSEUDOMAP 1
#else
#define STEROIDSLOG_HAVE_PSEUDOMAP 0
#endif

using namespace steroidslog;

TEST(Pseudomap, Fnv1aKnownVector) {
#if !STEROIDSLOG_HAVE_PSEUDOMAP
    GTEST_SKIP() << "pseudomap header not found; skipping.";
#else
    // A tiny sanity check for the constexpr hash:
    // "abc" FNV-1a 32-bit = 0x1A47E90B
    constexpr uint32_t h = fnv1a_32("abc");
    EXPECT_EQ(h, 0x1A47E90B);
#endif
}

TEST(Pseudomap, PutAndGet) {
#if !STEROIDSLOG_HAVE_PSEUDOMAP
    GTEST_SKIP() << "pseudomap header not found; skipping.";
#else
    constexpr uint32_t id = fnv1a_32("hello");
    pseudomap::get(id) = "world";
    auto&& sv = std::string_view(pseudomap::get(id));
    EXPECT_EQ(sv, "world");
#endif
}

TEST(Pseudomap, ReassignSameId) {
#if !STEROIDSLOG_HAVE_PSEUDOMAP
    GTEST_SKIP() << "pseudomap header not found; skipping.";
#else
    constexpr uint32_t id = fnv1a_32("key");

    // First write succeeds.
    pseudomap::get(id) = "once";
    auto&& sv1 = std::string_view(pseudomap::get(id));
    EXPECT_EQ(sv1, "once");

    // Second write is ignored by design (write-once per id).
    pseudomap::get(id) = "twice";
    auto&& sv2 = std::string_view(pseudomap::get(id));
    EXPECT_EQ(sv2, "once");
#endif
}

TEST(Pseudomap, MissingReturnsEmpty) {
#if !STEROIDSLOG_HAVE_PSEUDOMAP
    GTEST_SKIP() << "pseudomap header not found; skipping.";
#else
    constexpr uint32_t id = fnv1a_32("missing");
    auto&& sv = std::string_view(pseudomap::get(id));
    EXPECT_TRUE(sv.empty());
#endif
}
