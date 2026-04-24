#include "meta/node_manager.h"

namespace openfs
{

    uint64_t NodeManager::RegisterNode(const std::string &address, uint64_t capacity)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t id = next_node_id_++;
        NodeInfo info;
        info.node_id = id;
        info.address = address;
        info.capacity = capacity;
        info.online = true;
        info.last_heartbeat = NowNs();
        nodes_[id] = info;
        return id;
    }

    void NodeManager::UpdateHeartbeat(uint64_t node_id, uint64_t used)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodes_.find(node_id);
        if (it != nodes_.end())
        {
            it->second.used = used;
            it->second.last_heartbeat = NowNs();
        }
    }

    uint64_t NodeManager::GetAnyOnlineNode()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &[id, info] : nodes_)
        {
            if (info.online)
                return id;
        }
        return 0;
    }

    bool NodeManager::HasOnlineNodes() const
    {
        for (const auto &[id, info] : nodes_)
        {
            if (info.online)
                return true;
        }
        return false;
    }

} // namespace openfs