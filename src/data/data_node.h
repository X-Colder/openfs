#pragma once

#include "common/types.h"
#include "common/config.h"
#include "data/disk_pool.h"
#include "meta/meta_node_client.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace openfs
{

    // BlockLocation: tracks where a logical block was written
    struct BlockLocation
    {
        uint32_t disk_id = 0;
        uint64_t physical_offset = 0;
        uint32_t data_size = 0;
    };

    class DataNode
    {
    public:
        explicit DataNode(const DataNodeConfig &config);
        ~DataNode();

        Status Start();
        Status Stop();

        // Write a logical block to the disk pool
        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size,
                          uint32_t crc32,
                          uint32_t &out_disk_id, uint64_t &out_offset);

        // Read a logical block by disk_id + offset
        Status ReadBlock(uint32_t disk_id, uint64_t offset,
                         std::vector<char> &out_data, uint32_t &out_crc32,
                         uint64_t &out_block_id);

        // Delete a logical block
        Status DeleteBlock(uint64_t block_id);

        const DataNodeConfig &GetConfig() const { return config_; }
        uint64_t GetNodeId() const { return node_id_; }
        uint32_t GetBlockCount() const;
        DiskPool &GetDiskPool() { return disk_pool_; }

    private:
        void HeartbeatLoop();
        void RegisterWithMeta();
        void CollectBlockIds(std::vector<uint64_t> &block_ids);
        Status InitializeDisks();

        DataNodeConfig config_;
        DiskPool disk_pool_;
        std::unique_ptr<MetaNodeClient> meta_client_;

        uint64_t node_id_ = 0;
        std::atomic<bool> running_{false};
        std::thread heartbeat_thread_;
        std::mutex block_mutex_;

        // Local block index: block_id -> location info
        std::unordered_map<uint64_t, BlockLocation> block_locations_;
    };

} // namespace openfs