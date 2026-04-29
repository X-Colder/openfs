#include <gtest/gtest.h>
#include "common/types.h"

using namespace openfs;

// ============================================================
// StatusToString
// ============================================================
TEST(TypesTest, StatusToString_AllCodes)
{
    EXPECT_STREQ(StatusToString(Status::kOk), "OK");
    EXPECT_STREQ(StatusToString(Status::kNotFound), "NotFound");
    EXPECT_STREQ(StatusToString(Status::kAlreadyExists), "AlreadyExists");
    EXPECT_STREQ(StatusToString(Status::kInvalidArgument), "InvalidArgument");
    EXPECT_STREQ(StatusToString(Status::kIOError), "IOError");
    EXPECT_STREQ(StatusToString(Status::kNotDirectory), "NotDirectory");
    EXPECT_STREQ(StatusToString(Status::kNotEmpty), "NotEmpty");
    EXPECT_STREQ(StatusToString(Status::kNoSpace), "NoSpace");
    EXPECT_STREQ(StatusToString(Status::kCRCMismatch), "CRCMismatch");
    EXPECT_STREQ(StatusToString(Status::kInternal), "Internal");
}

TEST(TypesTest, StatusToString_UnknownReturnsUnknown)
{
    EXPECT_STREQ(StatusToString(static_cast<Status>(999)), "Unknown");
}

// ============================================================
// BlockLevelSize
// ============================================================
TEST(TypesTest, BlockLevelSize_AllLevels)
{
    EXPECT_EQ(BlockLevelSize(BlkLevel::L0), 64u * 1024);       // 64KB
    EXPECT_EQ(BlockLevelSize(BlkLevel::L1), 512u * 1024);      // 512KB
    EXPECT_EQ(BlockLevelSize(BlkLevel::L2), 4u * 1024 * 1024); // 4MB
    EXPECT_EQ(BlockLevelSize(BlkLevel::L3), 32u * 1024 * 1024); // 32MB
    EXPECT_EQ(BlockLevelSize(BlkLevel::L4), 256u * 1024 * 1024); // 256MB
}

// ============================================================
// SelectBlockLevel
// ============================================================
TEST(TypesTest, SelectBlockLevel_SmallFile_L0)
{
    EXPECT_EQ(SelectBlockLevel(0), BlkLevel::L0);
    EXPECT_EQ(SelectBlockLevel(100), BlkLevel::L0);
    EXPECT_EQ(SelectBlockLevel(64u * 1024), BlkLevel::L0); // boundary: <= 64KB
}

TEST(TypesTest, SelectBlockLevel_MediumSmallFile_L1)
{
    EXPECT_EQ(SelectBlockLevel(64u * 1024 + 1), BlkLevel::L1);
    EXPECT_EQ(SelectBlockLevel(512u * 1024), BlkLevel::L1); // boundary: <= 512KB
}

TEST(TypesTest, SelectBlockLevel_MediumFile_L2)
{
    EXPECT_EQ(SelectBlockLevel(512u * 1024 + 1), BlkLevel::L2);
    EXPECT_EQ(SelectBlockLevel(4u * 1024 * 1024), BlkLevel::L2); // boundary: <= 4MB
}

TEST(TypesTest, SelectBlockLevel_LargeFile_L3)
{
    EXPECT_EQ(SelectBlockLevel(4u * 1024 * 1024 + 1), BlkLevel::L3);
    EXPECT_EQ(SelectBlockLevel(32u * 1024 * 1024), BlkLevel::L3); // boundary: <= 32MB
}

TEST(TypesTest, SelectBlockLevel_VeryLargeFile_L3)
{
    // Files > 32MB still get L3 (split into multiple blocks)
    EXPECT_EQ(SelectBlockLevel(100u * 1024 * 1024), BlkLevel::L3);
    EXPECT_EQ(SelectBlockLevel(1ull * 1024 * 1024 * 1024), BlkLevel::L3); // 1GB
}

// ============================================================
// Segment constants
// ============================================================
TEST(TypesTest, SegmentConstants)
{
    EXPECT_EQ(kSegmentSize, 256ull * 1024 * 1024);
    EXPECT_EQ(kSegmentHeaderSize, 4096u);
    EXPECT_EQ(kSegmentFooterSize, 4096u);
    EXPECT_EQ(kBlockHeaderSize, 64u);
    EXPECT_EQ(kBlockAlignment, 4096u);
}

// ============================================================
// Inode defaults
// ============================================================
TEST(TypesTest, InodeDefaultValues)
{
    Inode inode;
    EXPECT_EQ(inode.inode_id, 0u);
    EXPECT_EQ(inode.file_type, InodeType::kRegular);
    EXPECT_EQ(inode.mode, 0644u);
    EXPECT_EQ(inode.uid, 0u);
    EXPECT_EQ(inode.gid, 0u);
    EXPECT_EQ(inode.size, 0u);
    EXPECT_EQ(inode.nlink, 1u);
    EXPECT_EQ(inode.block_level, BlkLevel::L2);
    EXPECT_FALSE(inode.is_packed);
    EXPECT_EQ(inode.parent_id, 0u);
}

// ============================================================
// BlockMeta defaults
// ============================================================
TEST(TypesTest, BlockMetaDefaultValues)
{
    BlockMeta bm;
    EXPECT_EQ(bm.block_id, 0u);
    EXPECT_EQ(bm.level, BlkLevel::L2);
    EXPECT_EQ(bm.size, 0u);
    EXPECT_EQ(bm.crc32, 0u);
    EXPECT_EQ(bm.replica_count, 1u);
    EXPECT_EQ(bm.access_count, 0u);
}

// ============================================================
// NowNs
// ============================================================
TEST(TypesTest, NowNsReturnsNonDecreasing)
{
    uint64_t t1 = NowNs();
    uint64_t t2 = NowNs();
    EXPECT_GE(t2, t1);
    EXPECT_GT(t1, 0u);
}