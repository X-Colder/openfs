#include "meta/block_allocator.h"

namespace openfs
{

    BlockAllocator::BlockAllocator(NodeManager &node_mgr) : node_mgr_(node_mgr) {}

    Status BlockAllocator::Allocate(uint32_t block_count, BlkLevel level,
                                    std::vector<BlockMeta> &out_blocks)
    {
        // Phase 1 single-node: assign all blocks to the single data node
        uint64_t target_node = node_mgr_.GetAnyOnlineNode();
        if (target_node == 0)
            return Status::kNoSpace;

        uint64_t seg_id = next_segment_id_.load(); // simplified: use current segment

        out_blocks.clear();
        out_blocks.reserve(block_count);
        for (uint32_t i = 0; i < block_count; ++i)
        {
            BlockMeta bm;
            bm.block_id = next_block_id_.fetch_add(1);
            bm.level = level;
            bm.size = BlockLevelSize(level);
            bm.node_id = target_node;
            bm.segment_id = seg_id;
            bm.create_time = NowNs();
            bm.replica_count = 1;
            out_blocks.push_back(bm);
        }
        return Status::kOk;
    }

} // namespace openfs