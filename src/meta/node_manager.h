#pragma once

#include "common/types.h"
#include <unordered_map>
#include <mutex>
#include <string>

namespace openfs
{

    struct NodeInfo
    {
        uint64_t node_id = 0;
        std::string address;
        uint64_t capacity = 0;
        uint64_t used = 0;
        bool online = true;
        uint64_t last_heartbeat = 0;
    };

    class NodeManager
    {
    public:
        NodeManager() = default;
        uint64_t RegisterNode(const std::string &address, uint64_t capacity);
        void UpdateHeartbeat(uint64_t node_id, uint64_t used);
        uint64_t GetAnyOnlineNode();
        bool HasOnlineNodes() const;

    private:
        std::unordered_map<uint64_t, NodeInfo> nodes_;
        mutable std::mutex mutex_;
        uint64_t next_node_id_ = 1;
    };

} // namespace openfs