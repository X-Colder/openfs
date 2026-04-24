#include "data/data_node.h"
#include "common/logging.h"
#include <thread>

namespace openfs
{

    DataNode::DataNode(const DataNodeConfig &config)
        : config_(config),
          segment_engine_(config.data_dir, config.segment_size)
    {
    }

    Status DataNode::Start()
    {
        LOG_INFO("Starting DataNode on {}", config_.listen_addr);
        LOG_INFO("Data directory: {}", config_.data_dir);
        LOG_INFO("Segment size: {} bytes", config_.segment_size);

        running_ = true;

        heartbeat_thread_ = std::thread([this]()
                                        { this->HeartbeatLoop(); });

        LOG_INFO("DataNode started successfully");
        return Status::kOk;
    }

    Status DataNode::Stop()
    {
        LOG_INFO("Stopping DataNode...");
        running_ = false;

        if (heartbeat_thread_.joinable())
        {
            heartbeat_thread_.join();
        }

        LOG_INFO("DataNode stopped");
        return Status::kOk;
    }

    void DataNode::HeartbeatLoop()
    {
        while (running_)
        {
            // TODO: send heartbeat to MetaNode via NodeService::Heartbeat RPC
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    Status DataNode::WriteBlock(uint64_t block_id, BlkLevel level,
                                const void *data, uint32_t data_size,
                                uint32_t crc32,
                                uint64_t &out_segment_id, uint64_t &out_offset)
    {
        return segment_engine_.WriteBlock(block_id, level, data, data_size, crc32, out_segment_id, out_offset);
    }

    Status DataNode::ReadBlock(uint64_t segment_id, uint64_t offset,
                               std::vector<char> &out_data, uint32_t &out_crc32)
    {
        return segment_engine_.ReadBlock(segment_id, offset, out_data, out_crc32);
    }

} // namespace openfs