#pragma once

#include "common/types.h"
#include "common/config.h"
#include "meta/namespace_manager.h"
#include "meta/inode_table.h"
#include "meta/block_map.h"
#include "meta/block_allocator.h"
#include "meta/node_manager.h"
#include <memory>

namespace openfs
{

    class MetaServer
    {
    public:
        explicit MetaServer(const MetaNodeConfig &config);
        ~MetaServer();

        Status Start();
        void Stop();

        NamespaceManager &GetNamespaceManager() { return *namespace_mgr_; }
        InodeTable &GetInodeTable() { return *inode_table_; }
        BlockMap &GetBlockMap() { return *block_map_; }
        BlockAllocator &GetBlockAllocator() { return *block_allocator_; }
        NodeManager &GetNodeManager() { return *node_mgr_; }

    private:
        MetaNodeConfig config_;
        std::unique_ptr<NamespaceManager> namespace_mgr_;
        std::unique_ptr<InodeTable> inode_table_;
        std::unique_ptr<BlockMap> block_map_;
        std::unique_ptr<BlockAllocator> block_allocator_;
        std::unique_ptr<NodeManager> node_mgr_;
        bool running_ = false;
    };

} // namespace openfs