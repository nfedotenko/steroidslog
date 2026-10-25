/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#include <gtest/gtest.h>

#include <cstring>
#include <thread>
#include <vector>

#include <misc/spsc_byte_ring.h>

using steroidslog::spsc_byte_ring;

namespace {

// Writes one record: u32 total size, then a u64 sequence number padded with
// a repeating byte derived from it.
bool write_record(auto& ring, uint64_t seq, std::size_t total) {
    std::byte* p = ring.reserve(total);
    if (!p) {
        return false;
    }
    const auto size32 = static_cast<uint32_t>(total);
    std::memcpy(p, &size32, sizeof(size32));
    std::memcpy(p + 4, &seq, sizeof(seq));
    std::memset(p + 12, static_cast<int>(seq & 0xFF), total - 12);
    ring.commit();
    return true;
}

// Reads one record written by write_record; returns false when empty.
bool read_record(auto& ring, uint64_t& seq_out, std::size_t& total_out) {
    const auto rec = ring.front();
    if (rec.empty()) {
        return false;
    }
    uint32_t size32 = 0;
    std::memcpy(&size32, rec.data(), sizeof(size32));
    EXPECT_EQ(size32, rec.size());
    std::memcpy(&seq_out, rec.data() + 4, sizeof(seq_out));
    for (std::size_t i = 12; i < rec.size(); ++i) {
        EXPECT_EQ(rec[i], static_cast<std::byte>(seq_out & 0xFF));
    }
    total_out = rec.size();
    ring.pop();
    return true;
}

} // namespace

TEST(ByteRing, RejectsBadSizes) {
    spsc_byte_ring<256> ring;
    EXPECT_EQ(ring.reserve(0), nullptr);
    EXPECT_EQ(ring.reserve(6), nullptr);   // not a multiple of 4
    EXPECT_EQ(ring.reserve(256), nullptr); // > capacity / 2
    EXPECT_NE(ring.reserve(128), nullptr); // == capacity / 2 is fine
}

TEST(ByteRing, SingleRecordRoundtrip) {
    spsc_byte_ring<256> ring;
    EXPECT_TRUE(ring.empty());
    ASSERT_TRUE(write_record(ring, 42, 24));
    EXPECT_FALSE(ring.empty());

    uint64_t seq = 0;
    std::size_t total = 0;
    ASSERT_TRUE(read_record(ring, seq, total));
    EXPECT_EQ(seq, 42u);
    EXPECT_EQ(total, 24u);
    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(read_record(ring, seq, total));
}

TEST(ByteRing, FillDrainRefill) {
    spsc_byte_ring<256> ring;
    uint64_t wr = 0;
    while (write_record(ring, wr, 32)) {
        ++wr;
    }
    EXPECT_GT(wr, 0u);

    uint64_t rd = 0;
    uint64_t seq = 0;
    std::size_t total = 0;
    while (read_record(ring, seq, total)) {
        EXPECT_EQ(seq, rd);
        ++rd;
    }
    EXPECT_EQ(rd, wr);

    // The ring is usable again after a full drain.
    ASSERT_TRUE(write_record(ring, 777, 32));
    ASSERT_TRUE(read_record(ring, seq, total));
    EXPECT_EQ(seq, 777u);
}

// Varying record sizes force wraps at many different offsets.
TEST(ByteRing, WrapAtManyOffsets) {
    spsc_byte_ring<256> ring;
    uint64_t wr = 0;
    uint64_t rd = 0;
    uint64_t seq = 0;
    std::size_t total = 0;

    for (int round = 0; round < 10'000; ++round) {
        const std::size_t sz = 16 + 4 * (round % 23);
        if (write_record(ring, wr, sz)) {
            ++wr;
        } else {
            ASSERT_TRUE(read_record(ring, seq, total));
            EXPECT_EQ(seq, rd);
            ++rd;
        }
    }
    while (read_record(ring, seq, total)) {
        EXPECT_EQ(seq, rd);
        ++rd;
    }
    EXPECT_EQ(rd, wr);
}

// Producer and consumer on different threads; every record must arrive
// intact and in order. Run under TSan/ASan in CI.
TEST(ByteRing, TwoThreadsStress) {
    constexpr uint64_t N = 200'000;
    spsc_byte_ring<4096> ring;

    std::thread producer([&] {
        uint64_t seq = 0;
        while (seq < N) {
            const std::size_t sz = 16 + 4 * (seq % 29);
            if (write_record(ring, seq, sz)) {
                ++seq;
            }
        }
    });

    uint64_t expect = 0;
    uint64_t seq = 0;
    std::size_t total = 0;
    while (expect < N) {
        if (read_record(ring, seq, total)) {
            ASSERT_EQ(seq, expect);
            ++expect;
        }
    }
    producer.join();
    EXPECT_TRUE(ring.empty());
}
