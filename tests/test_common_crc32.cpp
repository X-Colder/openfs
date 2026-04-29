#include <gtest/gtest.h>
#include "common/crc32.h"
#include <string>
#include <vector>

using namespace openfs;

// ============================================================
// ComputeCRC32 - basic correctness
// ============================================================
TEST(CRC32Test, EmptyInput)
{
    uint32_t crc = ComputeCRC32("", 0);
    EXPECT_EQ(crc, 0u); // CRC32 of empty data is 0
}

TEST(CRC32Test, KnownValue_HelloWorld)
{
    // "hello world" CRC32 = 0x0D4A1185 (standard CRC32)
    const char *data = "hello world";
    uint32_t crc = ComputeCRC32(data, 11);
    EXPECT_EQ(crc, 0x0D4A1185u);
}

TEST(CRC32Test, KnownValue_AllZeros_4Bytes)
{
    uint8_t data[4] = {0, 0, 0, 0};
    uint32_t crc = ComputeCRC32(data, 4);
    EXPECT_EQ(crc, 0x2144DF1Cu);
}

TEST(CRC32Test, KnownValue_SingleByte_A)
{
    const char *data = "A";
    uint32_t crc = ComputeCRC32(data, 1);
    EXPECT_EQ(crc, 0xD3D99E8Bu);
}

TEST(CRC32Test, Consistency_MultipleCalls)
{
    const char *data = "OpenFS distributed storage";
    size_t len = strlen(data);
    uint32_t crc1 = ComputeCRC32(data, len);
    uint32_t crc2 = ComputeCRC32(data, len);
    EXPECT_EQ(crc1, crc2);
}

TEST(CRC32Test, DifferentData_DifferentCRC)
{
    uint32_t crc1 = ComputeCRC32("foo", 3);
    uint32_t crc2 = ComputeCRC32("bar", 3);
    EXPECT_NE(crc1, crc2);
}

// ============================================================
// ComputeCRC32 - string overload
// ============================================================
TEST(CRC32Test, StringOverload_MatchesPointerVersion)
{
    std::string s = "test string for CRC";
    uint32_t crc_str = ComputeCRC32(s);
    uint32_t crc_ptr = ComputeCRC32(s.data(), s.size());
    EXPECT_EQ(crc_str, crc_ptr);
}

// ============================================================
// UpdateCRC32 - incremental computation
// ============================================================
TEST(UpdateCRC32Test, IncrementalMatchesBulk)
{
    // Compute CRC in one shot
    std::string data = "The quick brown fox jumps over the lazy dog";
    uint32_t bulk_crc = ComputeCRC32(data.data(), data.size());

    // Compute CRC incrementally in two parts
    size_t mid = data.size() / 2;
    uint32_t part1 = ComputeCRC32(data.data(), mid);
    uint32_t full_crc = UpdateCRC32(part1, data.data() + mid, data.size() - mid);

    EXPECT_EQ(bulk_crc, full_crc);
}

TEST(UpdateCRC32Test, IncrementalThreeParts)
{
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i & 0xFF);

    uint32_t bulk_crc = ComputeCRC32(data.data(), data.size());

    // Split into 3 parts
    uint32_t crc = ComputeCRC32(data.data(), 256);
    crc = UpdateCRC32(crc, data.data() + 256, 256);
    crc = UpdateCRC32(crc, data.data() + 512, 512);

    EXPECT_EQ(bulk_crc, crc);
}

// ============================================================
// Edge cases
// ============================================================
TEST(CRC32Test, LargeBuffer)
{
    std::vector<uint8_t> data(1024 * 1024, 0xAB); // 1MB of 0xAB
    uint32_t crc = ComputeCRC32(data.data(), data.size());
    EXPECT_NE(crc, 0u);
    // Second call should produce same result
    uint32_t crc2 = ComputeCRC32(data.data(), data.size());
    EXPECT_EQ(crc, crc2);
}