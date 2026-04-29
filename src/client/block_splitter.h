#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>

namespace openfs
{

    // Describes a single block slice to be written
    struct BlockSlice
    {
        uint64_t block_id = 0; // assigned by MetaNode
        BlkLevel level = BlkLevel::L2;
        uint64_t file_offset = 0; // byte offset within the original file
        uint32_t data_size = 0;   // byte size of this slice
        uint32_t crc32 = 0;       // CRC32 of the slice data
    };

    // Splits file data into block-sized slices based on block level selection
    class BlockSplitter
    {
    public:
        BlockSplitter() = default;

        // Split a file of given size into block slices.
        // The block level is auto-selected based on file_size.
        // Returns a list of BlockSlice descriptors (without block_id, which is
        // assigned later by MetaNode during AllocateBlocks).
        std::vector<BlockSlice> Split(uint64_t file_size, const void *data = nullptr);

        // Split with explicit block level override
        std::vector<BlockSlice> SplitWithLevel(uint64_t file_size, BlkLevel level,
                                               const void *data = nullptr);

        // Compute the number of blocks needed for a given file size and level
        static uint32_t BlockCount(uint64_t file_size, BlkLevel level);

        // Auto-select block level based on file size
        static BlkLevel SelectLevel(uint64_t file_size);
    };

} // namespace openfs