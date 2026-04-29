#pragma once

#include "common/types.h"
#include <grpcpp/grpcpp.h>
#include "data_service.grpc.pb.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace openfs
{

    // Connection pool to DataNodes.
    // Manages gRPC channels/stubs to multiple DataNodes, reusing connections.
    class DataConnection
    {
    public:
        DataConnection() = default;
        ~DataConnection() = default;

        // Write a block to a specific DataNode
        Status WriteBlock(const std::string &data_node_addr,
                          uint64_t block_id, uint64_t segment_id,
                          const void *data, uint32_t data_size,
                          uint32_t crc32,
                          uint64_t &out_segment_id, uint64_t &out_offset);

        // Read a block from a specific DataNode
        Status ReadBlock(const std::string &data_node_addr,
                         uint64_t segment_id, uint64_t offset,
                         std::vector<char> &out_data, uint32_t &out_crc32);

        // Delete a block from a specific DataNode
        Status DeleteBlock(const std::string &data_node_addr,
                           uint64_t block_id);

    private:
        // Get or create a stub for a DataNode address
        DataService::Stub *GetOrCreateStub(const std::string &addr);

        std::unordered_map<std::string, std::unique_ptr<DataService::Stub>> stubs_;
        std::unordered_map<std::string, std::shared_ptr<grpc::Channel>> channels_;
        mutable std::mutex mutex_;
    };

} // namespace openfs