#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace openfs
{

    // Manages a bitmap of physical blocks on a disk.
    // Each bit represents one physical block (4KB): 0 = free, 1 = allocated.
    // The bitmap itself is stored on disk at a known offset and loaded into memory.
    class BlockBitmap
    {
    public:
        BlockBitmap() = default;

        // Initialize bitmap for a given number of data blocks
        explicit BlockBitmap(uint64_t num_blocks);

        // Allocate consecutive blocks for a logical block of given size.
        // Returns the starting block index, or kInvalidBlockIndex on failure.
        uint64_t Allocate(uint32_t num_blocks);

        // Free consecutive blocks starting at start_block for num_blocks count
        void Free(uint64_t start_block, uint32_t num_blocks);

        // Check if a specific block is allocated
        bool IsAllocated(uint64_t block_index) const;

        // Set a specific block as allocated
        void SetAllocated(uint64_t block_index);

        // Set a specific block as free
        void SetFree(uint64_t block_index);

        // Get total number of data blocks
        uint64_t TotalBlocks() const { return num_blocks_; }

        // Get number of free blocks
        uint64_t FreeBlocks() const;

        // Get number of allocated blocks
        uint64_t AllocatedBlocks() const { return num_blocks_ - FreeBlocks(); }

        // Get the raw bitmap data for persistence
        const std::vector<uint8_t> &Data() const { return bitmap_; }

        // Load bitmap from raw data
        void Load(const std::vector<uint8_t> &data, uint64_t num_blocks);

        // Validate internal consistency
        bool Validate() const;

        static constexpr uint64_t kInvalidBlockIndex = UINT64_MAX;

    private:
        uint64_t num_blocks_ = 0;
        std::vector<uint8_t> bitmap_; // 1 bit per block

        // Find consecutive free blocks. Returns start index or kInvalidBlockIndex.
        uint64_t FindConsecutiveFree(uint32_t count) const;

        // Bit manipulation helpers
        static void SetBit(std::vector<uint8_t> &bm, uint64_t index);
        static void ClearBit(std::vector<uint8_t> &bm, uint64_t index);
        static bool GetBit(const std::vector<uint8_t> &bm, uint64_t index);
    };

} // namespace openfs