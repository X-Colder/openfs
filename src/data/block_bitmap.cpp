#include "data/block_bitmap.h"
#include "common/logging.h"
#include <algorithm>

namespace openfs
{

    BlockBitmap::BlockBitmap(uint64_t num_blocks)
        : num_blocks_(num_blocks),
          bitmap_((num_blocks + 7) / 8, 0)
    {
    }

    uint64_t BlockBitmap::Allocate(uint32_t count)
    {
        if (count == 0)
            return kInvalidBlockIndex;

        uint64_t start = FindConsecutiveFree(count);
        if (start == kInvalidBlockIndex)
            return kInvalidBlockIndex;

        for (uint32_t i = 0; i < count; ++i)
            SetBit(bitmap_, start + i);

        return start;
    }

    void BlockBitmap::Free(uint64_t start_block, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if (start_block + i < num_blocks_)
                ClearBit(bitmap_, start_block + i);
        }
    }

    bool BlockBitmap::IsAllocated(uint64_t block_index) const
    {
        if (block_index >= num_blocks_)
            return false;
        return GetBit(bitmap_, block_index);
    }

    void BlockBitmap::SetAllocated(uint64_t block_index)
    {
        if (block_index < num_blocks_)
            SetBit(bitmap_, block_index);
    }

    void BlockBitmap::SetFree(uint64_t block_index)
    {
        if (block_index < num_blocks_)
            ClearBit(bitmap_, block_index);
    }

    uint64_t BlockBitmap::FreeBlocks() const
    {
        uint64_t allocated = 0;
        for (uint64_t i = 0; i < num_blocks_; ++i)
        {
            if (GetBit(bitmap_, i))
                ++allocated;
        }
        return num_blocks_ - allocated;
    }

    void BlockBitmap::Load(const std::vector<uint8_t> &data, uint64_t num_blocks)
    {
        num_blocks_ = num_blocks;
        bitmap_ = data;
        // Resize if needed
        size_t needed = (num_blocks + 7) / 8;
        if (bitmap_.size() < needed)
            bitmap_.resize(needed, 0);
    }

    bool BlockBitmap::Validate() const
    {
        size_t expected = (num_blocks_ + 7) / 8;
        return bitmap_.size() >= expected;
    }

    uint64_t BlockBitmap::FindConsecutiveFree(uint32_t count) const
    {
        uint64_t consecutive = 0;
        uint64_t start = kInvalidBlockIndex;

        for (uint64_t i = 0; i < num_blocks_; ++i)
        {
            if (!GetBit(bitmap_, i))
            {
                if (consecutive == 0)
                    start = i;
                ++consecutive;
                if (consecutive >= count)
                    return start;
            }
            else
            {
                consecutive = 0;
                start = kInvalidBlockIndex;
            }
        }
        return kInvalidBlockIndex;
    }

    void BlockBitmap::SetBit(std::vector<uint8_t> &bm, uint64_t index)
    {
        bm[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
    }

    void BlockBitmap::ClearBit(std::vector<uint8_t> &bm, uint64_t index)
    {
        bm[index / 8] &= static_cast<uint8_t>(~(1 << (index % 8)));
    }

    bool BlockBitmap::GetBit(const std::vector<uint8_t> &bm, uint64_t index)
    {
        return (bm[index / 8] >> (index % 8)) & 1;
    }

} // namespace openfs