#include <gtest/gtest.h>
#include <filesystem>
#include "data/wal_manager.h"

using namespace openfs;

class WalManagerTest : public ::testing::Test
{
protected:
    std::string wal_path_;

    void SetUp() override
    {
        wal_path_ = std::filesystem::temp_directory_path().string() + "/openfs_test_wal_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    void TearDown() override
    {
        std::filesystem::remove(wal_path_);
    }
};

// ============================================================
// Open and Close
// ============================================================
TEST_F(WalManagerTest, OpenAndClose)
{
    WalManager wal;
    EXPECT_EQ(wal.Open(wal_path_, 64), Status::kOk);
    EXPECT_TRUE(wal.IsOpen());
    wal.Close();
    EXPECT_FALSE(wal.IsOpen());
}

// ============================================================
// Append and Commit
// ============================================================
TEST_F(WalManagerTest, AppendAndCommit)
{
    WalManager wal;
    ASSERT_EQ(wal.Open(wal_path_, 64), Status::kOk);

    uint64_t seq1;
    EXPECT_EQ(wal.AppendEntry(100, 4096, 1024, 0xDEADBEEF, seq1), Status::kOk);
    EXPECT_EQ(seq1, 1u);

    uint64_t seq2;
    EXPECT_EQ(wal.AppendEntry(200, 8192, 2048, 0xCAFEBABE, seq2), Status::kOk);
    EXPECT_EQ(seq2, 2u);

    // Commit the first entry
    EXPECT_EQ(wal.CommitEntry(seq1), Status::kOk);

    wal.Close();
}

// ============================================================
// Replay entries
// ============================================================
TEST_F(WalManagerTest, ReplayEntries)
{
    WalManager wal;
    ASSERT_EQ(wal.Open(wal_path_, 64), Status::kOk);

    uint64_t seq1, seq2;
    wal.AppendEntry(100, 4096, 1024, 0xDEADBEEF, seq1);
    wal.CommitEntry(seq1);
    wal.AppendEntry(200, 8192, 2048, 0xCAFEBABE, seq2);
    // seq2 not committed (simulating crash)

    wal.Close();

    // Reopen and replay
    WalManager wal2;
    ASSERT_EQ(wal2.Open(wal_path_, 64), Status::kOk);

    std::vector<WalManager::WalEntry> entries;
    EXPECT_EQ(wal2.Replay(entries), Status::kOk);
    EXPECT_EQ(entries.size(), 2u);

    // First entry should be committed
    EXPECT_EQ(entries[0].block_id, 100u);
    EXPECT_TRUE(entries[0].committed);

    // Second entry should be uncommitted
    EXPECT_EQ(entries[1].block_id, 200u);
    EXPECT_FALSE(entries[1].committed);

    wal2.Close();
}

// ============================================================
// Reset WAL
// ============================================================
TEST_F(WalManagerTest, ResetWal)
{
    WalManager wal;
    ASSERT_EQ(wal.Open(wal_path_, 64), Status::kOk);

    uint64_t seq;
    wal.AppendEntry(100, 4096, 1024, 0xDEADBEEF, seq);
    wal.CommitEntry(seq);

    EXPECT_EQ(wal.Reset(), Status::kOk);

    // After reset, replay should find no entries
    std::vector<WalManager::WalEntry> entries;
    EXPECT_EQ(wal.Replay(entries), Status::kOk);
    EXPECT_TRUE(entries.empty());

    wal.Close();
}

// ============================================================
// Commit non-existent entry
// ============================================================
TEST_F(WalManagerTest, CommitNonExistent)
{
    WalManager wal;
    ASSERT_EQ(wal.Open(wal_path_, 64), Status::kOk);

    // Try to commit a sequence that doesn't exist
    EXPECT_EQ(wal.CommitEntry(999), Status::kNotFound);

    wal.Close();
}

// ============================================================
// WAL full
// ============================================================
TEST_F(WalManagerTest, WalFull)
{
    WalManager wal;
    // Only 4 blocks = entries at offsets 4KB, 8KB, 12KB, 16KB => 4 entries max
    ASSERT_EQ(wal.Open(wal_path_, 4), Status::kOk);

    for (int i = 0; i < 4; ++i)
    {
        uint64_t seq;
        EXPECT_EQ(wal.AppendEntry(i + 1, 4096 * (i + 1), 1024, 0, seq), Status::kOk);
    }

    // 5th should fail
    uint64_t seq;
    EXPECT_EQ(wal.AppendEntry(999, 0, 0, 0, seq), Status::kNoSpace);

    wal.Close();
}