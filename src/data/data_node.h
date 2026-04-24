#pragma once

#include "common/types.h"
#include "common/config.h"
#include "data/segment_engine.h"
#include <string>
#include <thread>
#include <atomic>

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

        const DataNodeConfig &GetConfig() const { return config_; }

    private:
        void HeartbeatLoop();

        DataNodeConfig config_;
        SegmentEngine segment_engine_;

        std::atomic<bool> running_{false};
        std::thread heartbeat_thread_;
    };

} // namespace openfs