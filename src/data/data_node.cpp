#include "data/data_node.h"
#include "common/logging.h"
#include <thread>
#include <filesystem>

namespace openfs
{

    DataNode::DataNode(const DataNodeConfig &config)
        : config_(config)
    {
    }

    DataNode::~DataNode()
    {
        Stop();
    }

    Status DataNode::InitializeDisks()
    {
        if (config_.disk_paths.empty())
        {
            // Fallback: use data_dir as a single file-based disk
            std::filesystem::create_directories(config_.data_dir);
            std::string disk_path = config_.data_dir + "/disk0.ofs";
            Status s = disk_pool_.AddDisk(disk_path, node_id_, 0);
            if (s != Status::kOk)
            {
                LOG_ERROR("Failed to initialize default disk at {}", disk_path);
                return s;
            }
            LOG_INFO("Initialized default disk at {}", disk_path);
            return Status::kOk;
        }

        // Format and add each configured disk
        for (uint32_t i = 0; i < config_.disk_paths.size(); ++i)
        {
            const std::string &path = config_.disk_paths[i];
            Status s = disk_pool_.AddDisk(path, node_id_, i);
            if (s != Status::kOk)
            {
                LOG_ERROR("Failed to add disk {} at index {}", path, i);
                return s;
            }
            LOG_INFO("Initialized disk {} at {}", i, path);
        }

        LOG_INFO("Initialized {} disks", disk_pool_.DiskCount());
        return Status::kOk;
    }

    Status DataNode::Start()
    {
        LOG_INFO("Starting DataNode on {}", config_.listen_addr);

        // Initialize disk pool
        Status s = InitializeDisks();
        if (s != Status::kOk)
            return s;

        // Run recovery on all disks
        s = disk_pool_.RecoverAll();
        if (s != Status::kOk)
        {
            LOG_WARN("Recovery had issues, continuing anyway");
        }

        // Create MetaNode client and register
        meta_client_ = std::make_unique<MetaNodeClient>(config_.meta_addr);
        RegisterWithMeta();

        running_ = true;
        heartbeat_thread_ = std::thread([this]()
                                        { this->HeartbeatLoop(); });

        LOG_INFO("DataNode started successfully (node_id={}, disks={})", node_id_, disk_pool_.DiskCount());
        return Status::kOk;
    }

    Status DataNode::Stop()
    {
        if (!running_)
            return Status::kOk;

        LOG_INFO("Stopping DataNode...");
        running_ = false;

        if (heartbeat_thread_.joinable())
        {
            heartbeat_thread_.join();
        }

        disk_pool_.CloseAll();
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
                    0,
                    static_cast<uint32_t>(block_ids.size()),
                    0.0f);

                if (s != Status::kOk)
                {
                    LOG_WARN("Heartbeat to MetaNode failed");
                }
            }
            else if (node_id_ == 0)
            {
                RegisterWithMeta();
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void DataNode::CollectBlockIds(std::vector<uint64_t> &block_ids)
    {
        block_ids.clear();
        std::lock_guard<std::mutex> lock(block_mutex_);
        for (const auto &kv : block_locations_)
        {
            block_ids.push_back(kv.first);
        }
    }

    uint32_t DataNode::GetBlockCount() const
    {
        return static_cast<uint32_t>(block_locations_.size());
    }

    Status DataNode::WriteBlock(uint64_t block_id, BlkLevel level,
                                const void *data, uint32_t data_size,
                                uint32_t crc32,
                                uint32_t &out_disk_id, uint64_t &out_offset)
    {
        std::lock_guard<std::mutex> lock(block_mutex_);

        Status s = disk_pool_.WriteBlock(block_id, level, data, data_size, crc32,
                                         out_disk_id, out_offset);
        if (s != Status::kOk)
            return s;

        // Track block location
        BlockLocation loc;
        loc.disk_id = out_disk_id;
        loc.physical_offset = out_offset;
        loc.data_size = data_size;
        block_locations_[block_id] = loc;

        return Status::kOk;
    }

    Status DataNode::ReadBlock(uint32_t disk_id, uint64_t offset,
                               std::vector<char> &out_data, uint32_t &out_crc32,
                               uint64_t &out_block_id)
    {
        return disk_pool_.ReadBlock(disk_id, offset, out_data, out_crc32, out_block_id);
    }

    Status DataNode::DeleteBlock(uint64_t block_id)
    {
        std::lock_guard<std::mutex> lock(block_mutex_);

        auto it = block_locations_.find(block_id);
        if (it == block_locations_.end())
            return Status::kNotFound;

        const BlockLocation &loc = it->second;
        Status s = disk_pool_.DeleteBlock(loc.disk_id, loc.physical_offset, loc.data_size);
        if (s != Status::kOk)
            return s;

        block_locations_.erase(it);
        LOG_INFO("Deleted block {}", block_id);
        return Status::kOk;
    }

} // namespace openfs