#include <gtest/gtest.h>
#include "data/block_bitmap.h"

using namespace openfs;

// ============================================================
// Construction and basic state
// ============================================================
TEST(BlockBitmapTest, InitialState)
{
    BlockBitmap bm(1000);
    EXPECT_EQ(bm.TotalBlocks(), 1000u);
    EXPECT_EQ(bm.AllocatedBlocks(), 0u);
    EXPECT_EQ(bm.FreeBlocks(), 1000u);
    EXPECT_TRUE(bm.Validate());
}

TEST(BlockBitmapTest, EmptyBitmap)
{
    BlockBitmap bm;
    EXPECT_EQ(bm.TotalBlocks(), 0u);
    EXPECT_EQ(bm.FreeBlocks(), 0u);
}

// ============================================================
// Single block allocation
// ============================================================
TEST(BlockBitmapTest, AllocateSingleBlock)
{
    BlockBitmap bm(100);
    uint64_t idx = bm.Allocate(1);
    EXPECT_NE(idx, BlockBitmap::kInvalidBlockIndex);
    EXPECT_EQ(idx, 0u); // First free block
    EXPECT_TRUE(bm.IsAllocated(0));
    EXPECT_EQ(bm.FreeBlocks(), 99u);
}

TEST(BlockBitmapTest, AllocateMultipleConsecutive)
{
    BlockBitmap bm(100);
    uint64_t idx = bm.Allocate(4);
    EXPECT_NE(idx, BlockBitmap::kInvalidBlockIndex);
    EXPECT_EQ(idx, 0u);
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(bm.IsAllocated(i));
    EXPECT_FALSE(bm.IsAllocated(4));
    EXPECT_EQ(bm.FreeBlocks(), 96u);
}

// ============================================================
// Free and reallocate
// ============================================================
TEST(BlockBitmapTest, FreeAndReallocate)
{
    BlockBitmap bm(100);
    uint64_t idx = bm.Allocate(4);
    EXPECT_EQ(idx, 0u);

    bm.Free(0, 4);
    EXPECT_EQ(bm.FreeBlocks(), 100u);
    for (int i = 0; i < 4; ++i)
        EXPECT_FALSE(bm.IsAllocated(i));

    // Reallocate should get the same blocks
    uint64_t idx2 = bm.Allocate(4);
    EXPECT_EQ(idx2, 0u);
}

// ============================================================
// Allocation fills gaps
// ============================================================
TEST(BlockBitmapTest, AllocateFillsGap)
{
    BlockBitmap bm(100);

    // Allocate blocks 0-3 and 8-11
    bm.Allocate(4); // 0-3
    uint64_t idx2 = bm.Allocate(4); // 4-7
    EXPECT_EQ(idx2, 4u);

    // Free blocks 4-7
    bm.Free(4, 4);

    // Next allocation of 4 should fill the gap at 4-7
    uint64_t idx3 = bm.Allocate(4);
    EXPECT_EQ(idx3, 4u);
}

// ============================================================
// Allocation failure when no space
// ============================================================
TEST(BlockBitmapTest, AllocateNoSpace)
{
    BlockBitmap bm(8);
    uint64_t idx = bm.Allocate(8);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(bm.FreeBlocks(), 0u);

    // No more space
    uint64_t idx2 = bm.Allocate(1);
    EXPECT_EQ(idx2, BlockBitmap::kInvalidBlockIndex);
}

TEST(BlockBitmapTest, AllocateNoConsecutiveSpace)
{
    BlockBitmap bm(8);
    // Allocate every other block
    for (int i = 0; i < 8; i += 2)
        bm.SetAllocated(i);

    // Need 2 consecutive blocks but none available
    uint64_t idx = bm.Allocate(2);
    EXPECT_EQ(idx, BlockBitmap::kInvalidBlockIndex);
}

// ============================================================
// SetAllocated / SetFree individual blocks
// ============================================================
TEST(BlockBitmapTest, SetAllocatedAndFree)
{
    BlockBitmap bm(100);
    bm.SetAllocated(50);
    EXPECT_TRUE(bm.IsAllocated(50));
    bm.SetFree(50);
    EXPECT_FALSE(bm.IsAllocated(50));
}

TEST(BlockBitmapTest, IsAllocatedOutOfRange)
{
    BlockBitmap bm(100);
    EXPECT_FALSE(bm.IsAllocated(200));
    // Out of range should not crash
    bm.SetAllocated(200); // no-op
    bm.SetFree(200);      // no-op
}

// ============================================================
// Load from raw data
// ============================================================
TEST(BlockBitmapTest, LoadFromData)
{
    BlockBitmap bm1(100);
    bm1.Allocate(4);
    bm1.Allocate(4);

    auto raw = bm1.Data();

    BlockBitmap bm2;
    bm2.Load(raw, 100);
    EXPECT_EQ(bm2.TotalBlocks(), 100u);
    EXPECT_EQ(bm2.AllocatedBlocks(), 8u);
    EXPECT_TRUE(bm2.IsAllocated(0));
    EXPECT_TRUE(bm2.IsAllocated(7));
    EXPECT_FALSE(bm2.IsAllocated(8));
}

// ============================================================
// Zero-size allocation
// ============================================================
TEST(BlockBitmapTest, AllocateZeroBlocks)
{
    BlockBitmap bm(100);
    uint64_t idx = bm.Allocate(0);
    EXPECT_EQ(idx, BlockBitmap::kInvalidBlockIndex);
}

// ============================================================
// Large bitmap
// ============================================================
TEST(BlockBitmapTest, LargeBitmap)
{
    // Simulate a 12TB disk: 12TB / 4KB = ~3G blocks, use smaller for test
    BlockBitmap bm(100000);
    uint64_t idx = bm.Allocate(1024);
    EXPECT_NE(idx, BlockBitmap::kInvalidBlockIndex);
    EXPECT_EQ(bm.AllocatedBlocks(), 1024u);
}