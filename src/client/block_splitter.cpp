#include "client/block_splitter.h"
#include "common/crc32.h"

namespace openfs
{

    BlkLevel BlockSplitter::SelectLevel(uint64_t file_size)
    {
        return SelectBlockLevel(file_size);
    }

    uint32_t BlockSplitter::BlockCount(uint64_t file_size, BlkLevel level)
    {
        if (file_size == 0)
            return 0;
        uint32_t blk_size = BlockLevelSize(level);
        return static_cast<uint32_t>((file_size + blk_size - 1) / blk_size);
    }

    std::vector<BlockSlice> BlockSplitter::Split(uint64_t file_size, const void *data)
    {
        BlkLevel level = SelectLevel(file_size);
        return SplitWithLevel(file_size, level, data);
    }

    std::vector<BlockSlice> BlockSplitter::SplitWithLevel(uint64_t file_size, BlkLevel level,
                                                          const void *data)
    {
        std::vector<BlockSlice> slices;
        if (file_size == 0)
            return slices;

        uint32_t blk_size = BlockLevelSize(level);
        uint32_t count = BlockCount(file_size, level);
        slices.reserve(count);

        uint64_t remaining = file_size;
        uint64_t offset = 0;

        for (uint32_t i = 0; i < count; ++i)
        {
            BlockSlice slice;
            slice.level = level;
            slice.file_offset = offset;
            slice.data_size = static_cast<uint32_t>(std::min<uint64_t>(remaining, blk_size));

            // Compute CRC if data is provided
            if (data)
            {
                const uint8_t *ptr = static_cast<const uint8_t *>(data) + offset;
                slice.crc32 = ComputeCRC32(ptr, slice.data_size);
            }

            slices.push_back(slice);
            offset += slice.data_size;
            remaining -= slice.data_size;
        }

        return slices;
    }

} // namespace openfs