#include "meta/block_map.h"

namespace openfs
{

    Status BlockMap::AddBlocks(uint64_t inode_id, const std::vector<BlockMeta> &blocks)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &block_ids = inode_blocks_[inode_id];
        for (const auto &blk : blocks)
        {
            block_ids.push_back(blk.block_id);
            block_metas_[blk.block_id] = blk;
        }
        return Status::kOk;
    }

    Status BlockMap::GetBlocks(uint64_t inode_id, std::vector<BlockMeta> &blocks)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inode_blocks_.find(inode_id);
        if (it == inode_blocks_.end())
        {
            return Status::kNotFound;
        }
        blocks.clear();
        blocks.reserve(it->second.size());
        for (auto bid : it->second)
        {
            auto bit = block_metas_.find(bid);
            if (bit != block_metas_.end())
            {
                blocks.push_back(bit->second);
            }
        }
        return Status::kOk;
    }

    Status BlockMap::UpdateBlock(const BlockMeta &block)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = block_metas_.find(block.block_id);
        if (it == block_metas_.end())
            return Status::kNotFound;
        it->second = block;
        return Status::kOk;
    }

    Status BlockMap::RemoveBlocks(uint64_t inode_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inode_blocks_.find(inode_id);
        if (it == inode_blocks_.end())
        {
            return Status::kNotFound;
        }
        for (auto bid : it->second)
        {
            block_metas_.erase(bid);
        }
        inode_blocks_.erase(it);
        return Status::kOk;
    }

} // namespace openfs