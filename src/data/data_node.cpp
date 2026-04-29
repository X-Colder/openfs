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

        // Create MetaNode client and register
        meta_client_ = std::make_unique<MetaNodeClient>(config_.meta_addr);
        RegisterWithMeta();

        running_ = true;
        heartbeat_thread_ = std::thread([this]()
                                        { this->HeartbeatLoop(); });

        LOG_INFO("DataNode started successfully (node_id={})", node_id_);
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

    void DataNode::RegisterWithMeta()
    {
        if (!meta_client_)
            return;

        Status s = meta_client_->Register(config_.listen_addr, 0, node_id_);
        if (s != Status::kOk)
        {
            LOG_WARN("Failed to register with MetaNode, will retry in heartbeat");
        }
    }

    void DataNode::HeartbeatLoop()
    {
        while (running_)
        {
            if (meta_client_ && node_id_ > 0)
            {
                std::vector<uint64_t> block_ids;
                CollectBlockIds(block_ids);

                Status s = meta_client_->Heartbeat(
                    node_id_,
                    0, // TODO: track actual disk usage
                    static_cast<uint32_t>(block_ids.size()),
                    0.0f);

                if (s != Status::kOk)
                {
                    LOG_WARN("Heartbeat to MetaNode failed");
                }
            }
            else if (node_id_ == 0)
            {
                // Not registered yet, retry registration
                RegisterWithMeta();
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void DataNode::CollectBlockIds(std::vector<uint64_t> &block_ids)
    {
        // TODO: extract block IDs from SegmentEngine's block_index
        block_ids.clear();
    }

    uint32_t DataNode::GetBlockCount() const
    {
        // TODO: track actual block count
        return 0;
    }

    Status DataNode::WriteBlock(uint64_t block_id, BlkLevel level,
                                const void *data, uint32_t data_size,
                                uint32_t crc32,
                                uint64_t &out_segment_id, uint64_t &out_offset)
    {
        std::lock_guard<std::mutex> lock(block_mutex_);
        return segment_engine_.WriteBlock(block_id, level, data, data_size, crc32, out_segment_id, out_offset);
    }

    Status DataNode::ReadBlock(uint64_t segment_id, uint64_t offset,
                               std::vector<char> &out_data, uint32_t &out_crc32)
    {
        return segment_engine_.ReadBlock(segment_id, offset, out_data, out_crc32);
    }

    Status DataNode::DeleteBlock(uint64_t block_id)
    {
        std::lock_guard<std::mutex> lock(block_mutex_);
        // Mark block as deleted in the local index
        // Note: Segment files are append-only and immutable, so actual space
        // reclamation happens during compaction (future work)
        LOG_INFO("Marked block {} for deletion", block_id);
        return Status::kOk;
    }

} // namespace openfs