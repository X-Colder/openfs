#include <gtest/gtest.h>
#include "meta/block_map.h"

using namespace openfs;

class BlockMapTest : public ::testing::Test
{
protected:
    BlockMap block_map;
};

// ============================================================
// AddBlocks
// ============================================================
TEST_F(BlockMapTest, AddBlocks_SingleBlock)
{
    BlockMeta blk;
    blk.block_id = 100;
    blk.level = BlkLevel::L2;
    blk.size = 4 * 1024 * 1024;
    blk.node_id = 1;

    std::vector<BlockMeta> blocks = {blk};
    EXPECT_EQ(block_map.AddBlocks(10, blocks), Status::kOk);
}

TEST_F(BlockMapTest, AddBlocks_MultipleBlocks)
{
    std::vector<BlockMeta> blocks;
    for (int i = 0; i < 5; ++i)
    {
        BlockMeta blk;
        blk.block_id = 100 + i;
        blk.level = BlkLevel::L1;
        blk.size = 512 * 1024;
        blk.node_id = 1;
        blocks.push_back(blk);
    }

    EXPECT_EQ(block_map.AddBlocks(20, blocks), Status::kOk);

    // Verify all blocks are retrievable
    std::vector<BlockMeta> retrieved;
    EXPECT_EQ(block_map.GetBlocks(20, retrieved), Status::kOk);
    EXPECT_EQ(retrieved.size(), 5u);
}

TEST_F(BlockMapTest, AddBlocks_AppendToExistingInode)
{
    BlockMeta blk1;
    blk1.block_id = 100;
    blk1.level = BlkLevel::L2;
    blk1.node_id = 1;

    BlockMeta blk2;
    blk2.block_id = 101;
    blk2.level = BlkLevel::L2;
    blk2.node_id = 1;

    EXPECT_EQ(block_map.AddBlocks(10, {blk1}), Status::kOk);
    EXPECT_EQ(block_map.AddBlocks(10, {blk2}), Status::kOk);

    std::vector<BlockMeta> blocks;
    EXPECT_EQ(block_map.GetBlocks(10, blocks), Status::kOk);
    EXPECT_EQ(blocks.size(), 2u);
}

// ============================================================
// GetBlocks
// ============================================================
TEST_F(BlockMapTest, GetBlocks_Success)
{
    BlockMeta blk;
    blk.block_id = 200;
    blk.level = BlkLevel::L0;
    blk.size = 64 * 1024;
    blk.crc32 = 0xDEADBEEF;
    blk.node_id = 2;
    blk.segment_id = 5;
    blk.offset = 4096;

    block_map.AddBlocks(30, {blk});

    std::vector<BlockMeta> retrieved;
    EXPECT_EQ(block_map.GetBlocks(30, retrieved), Status::kOk);
    ASSERT_EQ(retrieved.size(), 1u);
    EXPECT_EQ(retrieved[0].block_id, 200u);
    EXPECT_EQ(retrieved[0].level, BlkLevel::L0);
    EXPECT_EQ(retrieved[0].crc32, 0xDEADBEEFu);
    EXPECT_EQ(retrieved[0].node_id, 2u);
    EXPECT_EQ(retrieved[0].segment_id, 5u);
    EXPECT_EQ(retrieved[0].offset, 4096u);
}

TEST_F(BlockMapTest, GetBlocks_InodeNotFound)
{
    std::vector<BlockMeta> blocks;
    EXPECT_EQ(block_map.GetBlocks(999, blocks), Status::kNotFound);
}

TEST_F(BlockMapTest, GetBlocks_EmptyInode)
{
    // Add and then remove
    BlockMeta blk;
    blk.block_id = 300;
    blk.node_id = 1;
    block_map.AddBlocks(40, {blk});
    block_map.RemoveBlocks(40);

    std::vector<BlockMeta> blocks;
    EXPECT_EQ(block_map.GetBlocks(40, blocks), Status::kNotFound);
}

// ============================================================
// UpdateBlock
// ============================================================
TEST_F(BlockMapTest, UpdateBlock_Success)
{
    BlockMeta blk;
    blk.block_id = 400;
    blk.level = BlkLevel::L2;
    blk.size = 0;
    blk.crc32 = 0;
    blk.offset = 0;

    block_map.AddBlocks(50, {blk});

    // Update after write confirms offset and crc
    BlockMeta updated;
    updated.block_id = 400;
    updated.level = BlkLevel::L2;
    updated.size = 4 * 1024 * 1024;
    updated.crc32 = 0xABCDEF01;
    updated.offset = 8192;
    updated.node_id = 1;

    EXPECT_EQ(block_map.UpdateBlock(updated), Status::kOk);

    std::vector<BlockMeta> retrieved;
    block_map.GetBlocks(50, retrieved);
    ASSERT_EQ(retrieved.size(), 1u);
    EXPECT_EQ(retrieved[0].size, 4u * 1024 * 1024);
    EXPECT_EQ(retrieved[0].crc32, 0xABCDEF01u);
    EXPECT_EQ(retrieved[0].offset, 8192u);
}

TEST_F(BlockMapTest, UpdateBlock_NotFound)
{
    BlockMeta blk;
    blk.block_id = 999;
    EXPECT_EQ(block_map.UpdateBlock(blk), Status::kNotFound);
}

// ============================================================
// RemoveBlocks
// ============================================================
TEST_F(BlockMapTest, RemoveBlocks_Success)
{
    BlockMeta blk;
    blk.block_id = 500;
    blk.node_id = 1;
    block_map.AddBlocks(60, {blk});

    EXPECT_EQ(block_map.RemoveBlocks(60), Status::kOk);

    std::vector<BlockMeta> blocks;
    EXPECT_EQ(block_map.GetBlocks(60, blocks), Status::kNotFound);
}

TEST_F(BlockMapTest, RemoveBlocks_NotFound)
{
    EXPECT_EQ(block_map.RemoveBlocks(999), Status::kNotFound);
}

TEST_F(BlockMapTest, RemoveBlocks_MultipleBlocksAllRemoved)
{
    std::vector<BlockMeta> blocks;
    for (int i = 0; i < 3; ++i)
    {
        BlockMeta blk;
        blk.block_id = 600 + i;
        blk.node_id = 1;
        blocks.push_back(blk);
    }
    block_map.AddBlocks(70, blocks);

    EXPECT_EQ(block_map.RemoveBlocks(70), Status::kOk);

    std::vector<BlockMeta> retrieved;
    EXPECT_EQ(block_map.GetBlocks(70, retrieved), Status::kNotFound);
}

// ============================================================
// Block ordering preserved
// ============================================================
TEST_F(BlockMapTest, AddBlocks_OrderPreserved)
{
    std::vector<BlockMeta> blocks;
    for (int i = 0; i < 10; ++i)
    {
        BlockMeta blk;
        blk.block_id = 700 + i;
        blk.offset = i * 4096;
        blk.node_id = 1;
        blocks.push_back(blk);
    }
    block_map.AddBlocks(80, blocks);

    std::vector<BlockMeta> retrieved;
    block_map.GetBlocks(80, retrieved);
    ASSERT_EQ(retrieved.size(), 10u);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(retrieved[i].block_id, 700u + i);
        EXPECT_EQ(retrieved[i].offset, static_cast<uint64_t>(i * 4096));
    }
}