#include "meta/meta_server.h"
#include "common/logging.h"

namespace openfs
{

    MetaServer::MetaServer(const MetaNodeConfig &config) : config_(config) {}

    MetaServer::~MetaServer() { Stop(); }

    Status MetaServer::Start()
    {
        LOG_INFO("MetaServer starting on {}", config_.listen_addr);

        inode_table_ = std::make_unique<InodeTable>();
        namespace_mgr_ = std::make_unique<NamespaceManager>(*inode_table_);
        block_map_ = std::make_unique<BlockMap>();
        node_mgr_ = std::make_unique<NodeManager>();
        block_allocator_ = std::make_unique<BlockAllocator>(*node_mgr_);

        running_ = true;
        LOG_INFO("MetaServer started successfully");
        return Status::kOk;
    }

    void MetaServer::Stop()
    {
        if (!running_)
            return;
        running_ = false;
        LOG_INFO("MetaServer stopped");
    }

} // namespace openfs