#pragma once

#include "common/types.h"
#include "common/config.h"
#include <grpcpp/grpcpp.h>
#include "node_service.grpc.pb.h"
#include <memory>
#include <string>

namespace openfs
{

    // Client for DataNode to communicate with MetaNode via NodeService
    class MetaNodeClient
    {
    public:
        explicit MetaNodeClient(const std::string &meta_addr);

        // Register this DataNode with the MetaNode
        Status Register(const std::string &address, uint64_t capacity, uint64_t &assigned_node_id);

        // Send heartbeat to MetaNode
        Status Heartbeat(uint64_t node_id, uint64_t used, uint32_t block_count, float cpu_load);

        // Report local blocks to MetaNode
        Status ReportBlocks(uint64_t node_id, const std::vector<uint64_t> &block_ids);

    private:
        std::shared_ptr<grpc::Channel> channel_;
        std::unique_ptr<NodeService::Stub> stub_;
    };

} // namespace openfs