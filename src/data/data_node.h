#pragma once

#include "common/types.h"
#include "common/config.h"
#include "data/segment_engine.h"
#include "meta/meta_node_client.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace openfs
{

    class DataNode
    {
    public:
        explicit DataNode(const DataNodeConfig &config);

        Status Start();
        Status Stop();

        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size,
                          uint32_t crc32,
                          uint64_t &out_segment_id, uint64_t &out_offset);

        Status ReadBlock(uint64_t segment_id, uint64_t offset,
                         std::vector<char> &out_data, uint32_t &out_crc32);

        Status DeleteBlock(uint64_t block_id);

        const DataNodeConfig &GetConfig() const { return config_; }
        uint64_t GetNodeId() const { return node_id_; }
        uint32_t GetBlockCount() const;

    private:
        void HeartbeatLoop();
        void RegisterWithMeta();
        void CollectBlockIds(std::vector<uint64_t> &block_ids);

        DataNodeConfig config_;
        SegmentEngine segment_engine_;
        std::unique_ptr<MetaNodeClient> meta_client_;

        uint64_t node_id_ = 0;
        std::atomic<bool> running_{false};
        std::thread heartbeat_thread_;
        std::mutex block_mutex_;
    };

} // namespace openfs