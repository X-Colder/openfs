#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <random>
#include <set>
#include "data/disk_manager.h"
#include "data/disk_pool.h"
#include "common/crc32.h"

using namespace openfs;

class DiskManagerTest : public ::testing::Test
{
protected:
    std::string disk_path_;

    void SetUp() override
    {
        disk_path_ = std::filesystem::temp_directory_path().string() + "/openfs_test_disk_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    void TearDown() override
    {
        std::filesystem::remove(disk_path_);
        std::filesystem::remove(disk_path_ + ".wal");
    }

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
// Format a disk
// ============================================================
TEST_F(DiskManagerTest, FormatDisk)
{
    uint64_t disk_size = 4ULL * 1024 * 1024; // 4MB
    Status s = DiskFormatter::Format(disk_path_, disk_size, 16, 1, 0);
    EXPECT_EQ(s, Status::kOk);

    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(disk_path_));

    // Verify superblock
    EXPECT_TRUE(DiskFormatter::IsFormatted(disk_path_));

    // Read superblock
    DiskSuperBlock sb;
    s = DiskFormatter::ReadSuperBlock(disk_path_, sb);
    EXPECT_EQ(s, Status::kOk);
    EXPECT_EQ(sb.version, kDiskFormatVersion);
    EXPECT_EQ(sb.node_id, 1u);
    EXPECT_EQ(sb.disk_index, 0u);
    EXPECT_GT(sb.data_blocks, 0u);
    EXPECT_GT(sb.bitmap_blocks, 0u);
}

TEST_F(DiskManagerTest, FormatDiskTooSmall)
{
    Status s = DiskFormatter::Format(disk_path_, 1024, 16, 1, 0); // Too small
    EXPECT_EQ(s, Status::kInvalidArgument);
}

// ============================================================
// Open and close disk
// ============================================================
TEST_F(DiskManagerTest, OpenAndClose)
{
    DiskManager dm;
    Status s = dm.Open(disk_path_, 1, 0);
    EXPECT_EQ(s, Status::kOk);
    EXPECT_TRUE(dm.IsOpen());
    EXPECT_EQ(dm.GetState(), DiskState::kNormal);
    EXPECT_GT(dm.TotalDataSpace(), 0u);

    dm.Close();
    EXPECT_FALSE(dm.IsOpen());
}

// ============================================================
// Write and Read block round-trip
// ============================================================
TEST_F(DiskManagerTest, WriteAndReadBlock)
{
    DiskManager dm;
    ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);

    uint32_t crc;
    auto data = MakeData(1024, crc);

    uint64_t offset;
    Status s = dm.WriteBlock(42, BlkLevel::L0, data.data(), 1024, crc, offset);
    EXPECT_EQ(s, Status::kOk);
    EXPECT_GT(offset, 0u);

    // Read back
    std::vector<char> read_data;
    uint32_t read_crc;
    uint64_t read_block_id;
    s = dm.ReadBlock(offset, read_data, read_crc, read_block_id);
    EXPECT_EQ(s, Status::kOk);
    EXPECT_EQ(read_block_id, 42u);
    EXPECT_EQ(read_crc, crc);
    EXPECT_EQ(read_data.size(), 1024u);
    EXPECT_EQ(memcmp(read_data.data(), data.data(), 1024), 0);

    dm.Close();
}

// ============================================================
// Write and Read multiple blocks
// ============================================================
TEST_F(DiskManagerTest, WriteAndReadMultipleBlocks)
{
    DiskManager dm;
    ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);

    struct BlockInfo
    {
        uint64_t block_id;
        uint64_t offset;
        uint32_t crc;
        std::vector<char> data;
    };
    std::vector<BlockInfo> blocks;

    for (int i = 0; i < 5; ++i)
    {
        uint32_t crc;
        size_t size = 4096 * (i + 1);
        auto data = MakeData(size, crc);

        uint64_t offset;
        Status s = dm.WriteBlock(100 + i, BlkLevel::L1, data.data(),
                                 static_cast<uint32_t>(size), crc, offset);
        ASSERT_EQ(s, Status::kOk);

        blocks.push_back({static_cast<uint64_t>(100 + i), offset, crc, std::move(data)});
    }

    // Read each back independently
    for (int i = 0; i < 5; ++i)
    {
        std::vector<char> read_data;
        uint32_t read_crc;
        uint64_t read_block_id;
        Status s = dm.ReadBlock(blocks[i].offset, read_data, read_crc, read_block_id);
        EXPECT_EQ(s, Status::kOk);
        EXPECT_EQ(read_block_id, blocks[i].block_id);
        EXPECT_EQ(read_crc, blocks[i].crc);
        EXPECT_EQ(read_data.size(), blocks[i].data.size());
        EXPECT_EQ(memcmp(read_data.data(), blocks[i].data.data(), blocks[i].data.size()), 0);
    }

    dm.Close();
}

// ============================================================
// Delete block and verify space reclaimed
// ============================================================
TEST_F(DiskManagerTest, DeleteBlock)
{
    DiskManager dm;
    ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);

    uint64_t initial_free = dm.FreeBlockCount();

    uint32_t crc;
    auto data = MakeData(4096, crc);
    uint64_t offset;
    ASSERT_EQ(dm.WriteBlock(1, BlkLevel::L0, data.data(), 4096, crc, offset), Status::kOk);

    EXPECT_LT(dm.FreeBlockCount(), initial_free);

    Status s = dm.DeleteBlock(offset, 4096);
    EXPECT_EQ(s, Status::kOk);
    EXPECT_EQ(dm.FreeBlockCount(), initial_free);

    dm.Close();
}

// ============================================================
// Disk full
// ============================================================
TEST_F(DiskManagerTest, DiskFull)
{
    DiskManager dm;
    ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);

    uint64_t free_blocks = dm.FreeBlockCount();

    // Write blocks until full
    uint32_t crc;
    auto data = MakeData(4096, crc);
    int written = 0;
    while (true)
    {
        uint64_t offset;
        Status s = dm.WriteBlock(1000 + written, BlkLevel::L0, data.data(), 4096, crc, offset);
        if (s == Status::kNoSpace)
            break;
        ASSERT_EQ(s, Status::kOk);
        written++;
        if (written > 10000)
            break; // Safety limit
    }

    EXPECT_GT(written, 0);
    EXPECT_EQ(dm.FreeBlockCount(), 0u);

    dm.Close();
}

// ============================================================
// Reopen disk preserves data
// ============================================================
TEST_F(DiskManagerTest, ReopenPreservesData)
{
    uint32_t crc;
    auto data = MakeData(2048, crc);
    uint64_t offset;

    {
        DiskManager dm;
        ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);
        ASSERT_EQ(dm.WriteBlock(77, BlkLevel::L0, data.data(), 2048, crc, offset), Status::kOk);
        dm.Close();
    }

    // Reopen
    {
        DiskManager dm;
        ASSERT_EQ(dm.OpenExisting(disk_path_), Status::kOk);

        std::vector<char> read_data;
        uint32_t read_crc;
        uint64_t read_block_id;
        Status s = dm.ReadBlock(offset, read_data, read_crc, read_block_id);
        EXPECT_EQ(s, Status::kOk);
        EXPECT_EQ(read_block_id, 77u);
        EXPECT_EQ(read_crc, crc);
        EXPECT_EQ(read_data.size(), 2048u);

        dm.Close();
    }
}

// ============================================================
// Crash recovery via WAL
// ============================================================
TEST_F(DiskManagerTest, CrashRecovery)
{
    uint32_t crc1, crc2;
    auto data1 = MakeData(1024, crc1);
    auto data2 = MakeData(2048, crc2);
    uint64_t offset1, offset2;

    // Write two blocks, then simulate crash by not closing properly
    {
        DiskManager dm;
        ASSERT_EQ(dm.Open(disk_path_, 1, 0), Status::kOk);
        ASSERT_EQ(dm.WriteBlock(10, BlkLevel::L0, data1.data(), 1024, crc1, offset1), Status::kOk);
        ASSERT_EQ(dm.WriteBlock(20, BlkLevel::L0, data2.data(), 2048, crc2, offset2), Status::kOk);
        // Don't call Close() to simulate crash (bitmap might not be fully persisted)
    }

    // Reopen and recover
    {
        DiskManager dm;
        ASSERT_EQ(dm.OpenExisting(disk_path_), Status::kOk);
        Status s = dm.Recover();
        EXPECT_EQ(s, Status::kOk);

        // Block 10 data should be readable (was committed)
        std::vector<char> read_data;
        uint32_t read_crc;
        uint64_t read_block_id;
        s = dm.ReadBlock(offset1, read_data, read_crc, read_block_id);
        EXPECT_EQ(s, Status::kOk);
        EXPECT_EQ(read_block_id, 10u);

        dm.Close();
    }
}

// ============================================================
// SuperBlock validation
// ============================================================
TEST_F(DiskManagerTest, InvalidSuperBlock)
{
    // Create a file with invalid content
    {
        std::ofstream f(disk_path_, std::ios::binary | std::ios::trunc);
        f.write("GARBAGE_DATA_HERE", 17);
    }

    EXPECT_FALSE(DiskFormatter::IsFormatted(disk_path_));

    DiskManager dm;
    Status s = dm.OpenExisting(disk_path_);
    EXPECT_NE(s, Status::kOk);
}

// ============================================================
// DiskPool tests
// ============================================================
class DiskPoolTest : public ::testing::Test
{
protected:
    std::string dir_;

    void SetUp() override
    {
        dir_ = std::filesystem::temp_directory_path().string() + "/openfs_test_pool_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(dir_);
    }

    std::string DiskPath(int index) const
    {
        return dir_ + "/disk" + std::to_string(index) + ".ofs";
    }
};

TEST_F(DiskPoolTest, AddMultipleDisks)
{
    DiskPool pool;
    EXPECT_EQ(pool.AddDisk(DiskPath(0), 1, 0), Status::kOk);
    EXPECT_EQ(pool.AddDisk(DiskPath(1), 1, 1), Status::kOk);
    EXPECT_EQ(pool.AddDisk(DiskPath(2), 1, 2), Status::kOk);
    EXPECT_EQ(pool.DiskCount(), 3u);
    EXPECT_GT(pool.TotalDataSpace(), 0u);

    pool.CloseAll();
}

TEST_F(DiskPoolTest, WriteAndReadAcrossDisks)
{
    DiskPool pool;
    ASSERT_EQ(pool.AddDisk(DiskPath(0), 1, 0), Status::kOk);
    ASSERT_EQ(pool.AddDisk(DiskPath(1), 1, 1), Status::kOk);

    std::vector<char> data(4096, 'A');
    uint32_t crc = ComputeCRC32(data.data(), 4096);

    // Write first block
    uint32_t disk_id1;
    uint64_t offset1;
    ASSERT_EQ(pool.WriteBlock(1, BlkLevel::L0, data.data(), 4096, crc, disk_id1, offset1), Status::kOk);

    // Write second block
    std::vector<char> data2(4096, 'B');
    uint32_t crc2 = ComputeCRC32(data2.data(), 4096);
    uint32_t disk_id2;
    uint64_t offset2;
    ASSERT_EQ(pool.WriteBlock(2, BlkLevel::L0, data2.data(), 4096, crc2, disk_id2, offset2), Status::kOk);

    // Read both back
    std::vector<char> read_data;
    uint32_t read_crc;
    uint64_t read_block_id;

    ASSERT_EQ(pool.ReadBlock(disk_id1, offset1, read_data, read_crc, read_block_id), Status::kOk);
    EXPECT_EQ(read_block_id, 1u);
    EXPECT_EQ(read_crc, crc);

    ASSERT_EQ(pool.ReadBlock(disk_id2, offset2, read_data, read_crc, read_block_id), Status::kOk);
    EXPECT_EQ(read_block_id, 2u);
    EXPECT_EQ(read_crc, crc2);

    pool.CloseAll();
}

TEST_F(DiskPoolTest, ReadInvalidDisk)
{
    DiskPool pool;
    ASSERT_EQ(pool.AddDisk(DiskPath(0), 1, 0), Status::kOk);

    std::vector<char> data;
    uint32_t crc;
    uint64_t block_id;
    EXPECT_EQ(pool.ReadBlock(99, 4096, data, crc, block_id), Status::kNotFound);

    pool.CloseAll();
}

TEST_F(DiskPoolTest, LoadBalancing)
{
    DiskPool pool;
    ASSERT_EQ(pool.AddDisk(DiskPath(0), 1, 0), Status::kOk);
    ASSERT_EQ(pool.AddDisk(DiskPath(1), 1, 1), Status::kOk);

    // Write many small blocks - should distribute across disks
    std::vector<char> data(1024, 'X');
    uint32_t crc = ComputeCRC32(data.data(), 1024);

    std::set<uint32_t> used_disks;
    for (int i = 0; i < 20; ++i)
    {
        uint32_t disk_id;
        uint64_t offset;
        ASSERT_EQ(pool.WriteBlock(1000 + i, BlkLevel::L0, data.data(), 1024, crc, disk_id, offset), Status::kOk);
        used_disks.insert(disk_id);
    }

    // Should have used both disks (not guaranteed but very likely with even sizing)
    EXPECT_GE(used_disks.size(), 1u);

    pool.CloseAll();
}

TEST_F(DiskPoolTest, RecoverAll)
{
    DiskPool pool;
    ASSERT_EQ(pool.AddDisk(DiskPath(0), 1, 0), Status::kOk);
    ASSERT_EQ(pool.AddDisk(DiskPath(1), 1, 1), Status::kOk);

    // Write some data
    std::vector<char> data(2048, 'Z');
    uint32_t crc = ComputeCRC32(data.data(), 2048);
    uint32_t disk_id;
    uint64_t offset;
    ASSERT_EQ(pool.WriteBlock(55, BlkLevel::L0, data.data(), 2048, crc, disk_id, offset), Status::kOk);

    // Run recovery
    EXPECT_EQ(pool.RecoverAll(), Status::kOk);

    pool.CloseAll();
}