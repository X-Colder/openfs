#pragma once

#include "common/types.h"
#include "meta/node_manager.h"
#include <vector>
#include <atomic>

namespace openfs
{

    // Allocates blocks for file writes - selects target DataNode and assigns block IDs
    class BlockAllocator
    {
    public:
        explicit BlockAllocator(NodeManager &node_mgr);
        ~BlockAllocator() = default;

        // Allocate block_count blocks at the given level, returns block metas with
        // assigned block_id and target node_id (single-node mode: always node 1)
        Status Allocate(uint32_t block_count, BlkLevel level,
                        std::vector<BlockMeta> &out_blocks);

    private:
        NodeManager &node_mgr_;
        std::atomic<uint64_t> next_block_id_{1};
        std::atomic<uint64_t> next_segment_id_{1};
    };

} // namespace openfs