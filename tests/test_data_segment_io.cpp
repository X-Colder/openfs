#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include "data/segment_engine.h"
#include "common/crc32.h"

using namespace openfs;

class SegmentEngineTest : public ::testing::Test
{
protected:
    std::string test_dir_;
    std::unique_ptr<SegmentEngine> engine;

    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path().string() + "/openfs_test_seg_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(test_dir_);
        engine = std::make_unique<SegmentEngine>(test_dir_);
    }

    void TearDown() override
    {
        engine.reset();
        std::filesystem::remove_all(test_dir_);
    }

    // Helper: generate random data with known CRC
    std::vector<char> MakeData(size_t size, uint32_t &out_crc)
    {
        std::vector<char> data(size);
        std::mt19937 rng(42);
        std::generate(data.begin(), data.end(), [&]()
                      { return static_cast<char>(rng()); });
        out_crc = ComputeCRC32(data.data(), static_cast<uint32_t>(data.size()));
        return data;
    }
};

// ============================================================
// WriteBlock + ReadBlock round-trip
// ============================================================
TEST_F(SegmentEngineTest, WriteAndRead_SmallBlock)
{
    uint32_t crc;
    auto data = MakeData(1024, crc);

    uint64_t seg_id, offset;
    EXPECT_EQ(engine->WriteBlock(1, BlkLevel::L0, data.data(), 1024, crc, seg_id, offset),
              Status::kOk);
    EXPECT_GT(seg_id, 0u);
    EXPECT_GT(offset, 0u);

    std::vector<char> read_data;
    uint32_t read_crc;
    EXPECT_EQ(engine->ReadBlock(seg_id, offset, read_data, read_crc), Status::kOk);
    EXPECT_EQ(read_crc, crc);
    EXPECT_EQ(read_data.size(), 1024u);
    EXPECT_EQ(memcmp(read_data.data(), data.data(), 1024), 0);
}

TEST_F(SegmentEngineTest, WriteAndRead_MultipleBlocks)
{
    std::vector<uint64_t> seg_ids;
    std::vector<uint64_t> offsets;
    std::vector<uint32_t> crcs;
    std::vector<std::vector<char>> all_data;

    for (int i = 0; i < 5; ++i)
    {
        uint32_t crc;
        auto data = MakeData(4096 * (i + 1), crc);
        crcs.push_back(crc);
        all_data.push_back(data);

        uint64_t seg_id, offset;
        EXPECT_EQ(engine->WriteBlock(100 + i, BlkLevel::L1, data.data(),
                                      static_cast<uint32_t>(data.size()), crc, seg_id, offset),
                  Status::kOk);
        seg_ids.push_back(seg_id);
        offsets.push_back(offset);
    }

    // Read each block back and verify
    for (int i = 0; i < 5; ++i)
    {
        std::vector<char> read_data;
        uint32_t read_crc;
        EXPECT_EQ(engine->ReadBlock(seg_ids[i], offsets[i], read_data, read_crc),
                  Status::kOk);
        EXPECT_EQ(read_crc, crcs[i]);
        EXPECT_EQ(read_data.size(), all_data[i].size());
        EXPECT_EQ(memcmp(read_data.data(), all_data[i].data(), all_data[i].size()), 0);
    }
}

TEST_F(SegmentEngineTest, WriteAndRead_LargeBlock)
{
    uint32_t crc;
    size_t size = 2 * 1024 * 1024; // 2MB
    auto data = MakeData(size, crc);

    uint64_t seg_id, offset;
    EXPECT_EQ(engine->WriteBlock(200, BlkLevel::L2, data.data(),
                                  static_cast<uint32_t>(size), crc, seg_id, offset),
              Status::kOk);

    std::vector<char> read_data;
    uint32_t read_crc;
    EXPECT_EQ(engine->ReadBlock(seg_id, offset, read_data, read_crc), Status::kOk);
    EXPECT_EQ(read_data.size(), size);
    EXPECT_EQ(read_crc, crc);
}

// ============================================================
// ReadBlock error cases
// ============================================================
TEST_F(SegmentEngineTest, ReadBlock_NonexistentSegment)
{
    std::vector<char> data;
    uint32_t crc;
    EXPECT_EQ(engine->ReadBlock(9999, 4096, data, crc), Status::kNotFound);
}

TEST_F(SegmentEngineTest, ReadBlock_InvalidOffset)
{
    // First write something to create segment 1
    uint32_t crc;
    auto data = MakeData(1024, crc);
    uint64_t seg_id, offset;
    engine->WriteBlock(300, BlkLevel::L0, data.data(), 1024, crc, seg_id, offset);

    // Read at invalid offset
    std::vector<char> read_data;
    uint32_t read_crc;
    EXPECT_EQ(engine->ReadBlock(seg_id, 999999, read_data, read_crc), Status::kIOError);
}

// ============================================================
// CRC verification
// ============================================================
TEST_F(SegmentEngineTest, WriteBlock_CRCStoredCorrectly)
{
    uint32_t crc = 0xCAFEBABE;
    std::vector<char> data(4096, 'A');

    uint64_t seg_id, offset;
    engine->WriteBlock(400, BlkLevel::L0, data.data(), 4096, crc, seg_id, offset);

    std::vector<char> read_data;
    uint32_t read_crc;
    engine->ReadBlock(seg_id, offset, read_data, read_crc);
    EXPECT_EQ(read_crc, 0xCAFEBABEu);
}

// ============================================================
// Auto segment creation
// ============================================================
TEST_F(SegmentEngineTest, AutoCreateSegment_OnFirstWrite)
{
    uint32_t crc;
    auto data = MakeData(1024, crc);

    uint64_t seg_id, offset;
    engine->WriteBlock(500, BlkLevel::L0, data.data(), 1024, crc, seg_id, offset);

    // Should create segment file on disk
    std::ostringstream seg_path_oss;
    seg_path_oss << test_dir_ << "/segment_" << std::setw(6) << std::setfill('0') << seg_id << ".dat";
    std::string seg_path = seg_path_oss.str();
    EXPECT_TRUE(std::filesystem::exists(seg_path));
}

// ============================================================
// Block ID uniqueness in index
// ============================================================
TEST_F(SegmentEngineTest, WriteBlock_DifferentBlockIds_Tracked)
{
    uint32_t crc;
    auto data = MakeData(1024, crc);

    uint64_t seg1, off1, seg2, off2;
    engine->WriteBlock(600, BlkLevel::L0, data.data(), 1024, crc, seg1, off1);
    engine->WriteBlock(601, BlkLevel::L0, data.data(), 1024, crc, seg2, off2);

    // Both should be readable
    std::vector<char> r1, r2;
    uint32_t c1, c2;
    EXPECT_EQ(engine->ReadBlock(seg1, off1, r1, c1), Status::kOk);
    EXPECT_EQ(engine->ReadBlock(seg2, off2, r2, c2), Status::kOk);
}

// ============================================================
// Empty data
// ============================================================
TEST_F(SegmentEngineTest, WriteBlock_ZeroSizeData)
{
    uint64_t seg_id, offset;
    // Zero-size block write should still succeed (metadata only)
    Status s = engine->WriteBlock(700, BlkLevel::L0, nullptr, 0, 0, seg_id, offset);
    // May return kOk or kInvalidArgument depending on implementation
    // The important thing is no crash
    EXPECT_TRUE(s == Status::kOk || s == Status::kInvalidArgument);
}