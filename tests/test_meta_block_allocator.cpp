#include <gtest/gtest.h>
#include "meta/block_allocator.h"
#include "meta/node_manager.h"

using namespace openfs;

class BlockAllocatorTest : public ::testing::Test
{
protected:
    NodeManager node_mgr;
    BlockAllocator allocator{node_mgr};

    void SetUp() override
    {
        // Register a DataNode so allocator can find one
        node_mgr.RegisterNode("localhost:50051", 1024 * 1024 * 1024);
    }
};

// ============================================================
// Allocate
// ============================================================
TEST_F(BlockAllocatorTest, Allocate_SingleBlock)
{
    std::vector<BlockMeta> blocks;
    EXPECT_EQ(allocator.Allocate(1, BlkLevel::L2, blocks), Status::kOk);
    ASSERT_EQ(blocks.size(), 1u);

    const auto &blk = blocks[0];
    EXPECT_GT(blk.block_id, 0u);
    EXPECT_EQ(blk.level, BlkLevel::L2);
    EXPECT_EQ(blk.size, BlockLevelSize(BlkLevel::L2));
    EXPECT_GT(blk.node_id, 0u);
    EXPECT_GT(blk.segment_id, 0u);
}

TEST_F(BlockAllocatorTest, Allocate_MultipleBlocks)
{
    std::vector<BlockMeta> blocks;
    EXPECT_EQ(allocator.Allocate(5, BlkLevel::L1, blocks), Status::kOk);
    ASSERT_EQ(blocks.size(), 5u);

    for (const auto &blk : blocks)
    {
        EXPECT_EQ(blk.level, BlkLevel::L1);
        EXPECT_EQ(blk.size, BlockLevelSize(BlkLevel::L1));
        EXPECT_GT(blk.node_id, 0u);
    }
}

TEST_F(BlockAllocatorTest, Allocate_UniqueBlockIds)
{
    std::vector<BlockMeta> batch1, batch2;
    allocator.Allocate(3, BlkLevel::L2, batch1);
    allocator.Allocate(3, BlkLevel::L2, batch2);

    std::set<uint64_t> all_ids;
    for (const auto &blk : batch1)
    {
        EXPECT_TRUE(all_ids.insert(blk.block_id).second)
            << "Duplicate block_id: " << blk.block_id;
    }
    for (const auto &blk : batch2)
    {
        EXPECT_TRUE(all_ids.insert(blk.block_id).second)
            << "Duplicate block_id: " << blk.block_id;
    }
    EXPECT_EQ(all_ids.size(), 6u);
}

TEST_F(BlockAllocatorTest, Allocate_AllBlockLevels)
{
    for (int lvl = 0; lvl <= 4; ++lvl)
    {
        auto level = static_cast<BlkLevel>(lvl);
        std::vector<BlockMeta> blocks;
        EXPECT_EQ(allocator.Allocate(1, level, blocks), Status::kOk);
        ASSERT_EQ(blocks.size(), 1u);
        EXPECT_EQ(blocks[0].level, level);
        EXPECT_EQ(blocks[0].size, BlockLevelSize(level));
    }
}

TEST_F(BlockAllocatorTest, Allocate_ZeroCount)
{
    std::vector<BlockMeta> blocks;
    EXPECT_EQ(allocator.Allocate(0, BlkLevel::L2, blocks), Status::kOk);
    EXPECT_TRUE(blocks.empty());
}

TEST_F(BlockAllocatorTest, Allocate_NoOnlineNodes_ReturnsNoSpace)
{
    NodeManager empty_mgr;
    BlockAllocator empty_allocator{empty_mgr};

    std::vector<BlockMeta> blocks;
    EXPECT_EQ(empty_allocator.Allocate(1, BlkLevel::L2, blocks), Status::kNoSpace);
}

TEST_F(BlockAllocatorTest, Allocate_BlockLevelSizes)
{
    // Verify the block size matches the level specification
    struct
    {
        BlkLevel level;
        uint32_t expected_size;
    } cases[] = {
        {BlkLevel::L0, 64u * 1024},
        {BlkLevel::L1, 512u * 1024},
        {BlkLevel::L2, 4u * 1024 * 1024},
        {BlkLevel::L3, 32u * 1024 * 1024},
        {BlkLevel::L4, 256u * 1024 * 1024},
    };

    for (const auto &c : cases)
    {
        std::vector<BlockMeta> blocks;
        allocator.Allocate(1, c.level, blocks);
        ASSERT_EQ(blocks.size(), 1u);
        EXPECT_EQ(blocks[0].size, c.expected_size)
            << "Block size mismatch for level " << static_cast<int>(c.level);
    }
}