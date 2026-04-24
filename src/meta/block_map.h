#pragma once

#include "common/types.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace openfs
{

    // Maps inode_id -> ordered list of block_ids, and block_id -> BlockMeta
    class BlockMap
    {
    public:
        BlockMap() = default;
        ~BlockMap() = default;

        // Add block(s) for a file
        Status AddBlocks(uint64_t inode_id, const std::vector<BlockMeta> &blocks);
        // Get all blocks for a file
        Status GetBlocks(uint64_t inode_id, std::vector<BlockMeta> &blocks);
        // Update a single block's metadata (e.g., after write confirms offset)
        Status UpdateBlock(const BlockMeta &block);
        // Remove all blocks for a file
        Status RemoveBlocks(uint64_t inode_id);

    private:
        // inode_id -> [block_id, block_id, ...]
        std::unordered_map<uint64_t, std::vector<uint64_t>> inode_blocks_;
        // block_id -> BlockMeta
        std::unordered_map<uint64_t, BlockMeta> block_metas_;
        std::mutex mutex_;
    };

} // namespace openfs