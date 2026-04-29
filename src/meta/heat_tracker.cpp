#include "meta/heat_tracker.h"
#include "common/logging.h"

namespace openfs
{

    HeatTracker::HeatTracker(BlockMap &block_map, NodeManager &node_mgr)
        : block_map_(block_map), node_mgr_(node_mgr)
    {
    }

    HeatTracker::~HeatTracker()
    {
        Stop();
    }

    Status HeatTracker::Start()
    {
        if (running_)
            return Status::kOk;

        running_ = true;
        scan_thread_ = std::thread([this]()
                                   { this->ScanLoop(); });
        LOG_INFO("HeatTracker started, scan_interval={}s", scan_interval_sec_);
        return Status::kOk;
    }

    Status HeatTracker::Stop()
    {
        if (!running_)
            return Status::kOk;

        running_ = false;
        if (scan_thread_.joinable())
            scan_thread_.join();

        LOG_INFO("HeatTracker stopped");
        return Status::kOk;
    }

    void HeatTracker::OnBlockAccess(uint64_t block_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &entry = heat_map_[block_id];
        entry.block_id = block_id;
        entry.access_count++;
        entry.recent_accesses++;
        entry.last_access_time = NowNs();
    }

    HeatEntry HeatTracker::GetHeatInfo(uint64_t block_id) const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
        auto it = heat_map_.find(block_id);
        if (it != heat_map_.end())
            return it->second;
        return HeatEntry{};
    }

    std::vector<uint64_t> HeatTracker::GetHotBlocks() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
        std::vector<uint64_t> result;
        for (const auto &kv : heat_map_)
        {
            if (kv.second.is_hot)
                result.push_back(kv.first);
        }
        return result;
    }

    std::vector<uint64_t> HeatTracker::GetColdBlocks() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
        std::vector<uint64_t> result;
        for (const auto &kv : heat_map_)
        {
            if (!kv.second.is_hot && kv.second.recent_accesses < cold_threshold_)
                result.push_back(kv.first);
        }
        return result;
    }

    void HeatTracker::ScanLoop()
    {
        while (running_)
        {
            std::this_thread::sleep_for(std::chrono::seconds(scan_interval_sec_));
            if (!running_)
                break;

            ApplyHeatDecay();
            ProcessHotBlocks();
            ProcessColdBlocks();
        }
    }

    void HeatTracker::ApplyHeatDecay()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &kv : heat_map_)
        {
            auto &entry = kv.second;
            // Decay recent accesses
            entry.recent_accesses = static_cast<uint32_t>(
                entry.recent_accesses * decay_factor_);

            // Update hot/cold status
            entry.is_hot = (entry.recent_accesses >= hot_threshold_);
        }
    }

    void HeatTracker::ProcessHotBlocks()
    {
        // Hot blocks should have replicas >= 2
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &kv : heat_map_)
        {
            if (!kv.second.is_hot)
                continue;

            uint64_t block_id = kv.first;
            // Check if block needs more replicas
            // TODO: Get block metadata from BlockMap and check replica_count
            // If replica_count < 2, trigger a replica upgrade
            LOG_DEBUG("HeatTracker: block {} is hot (accesses={}), consider upgrading replicas",
                      block_id, kv.second.recent_accesses);
        }
    }

    void HeatTracker::ProcessColdBlocks()
    {
        // Cold blocks with >1 replica should drop to 1
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &kv : heat_map_)
        {
            if (kv.second.is_hot || kv.second.recent_accesses >= cold_threshold_)
                continue;

            uint64_t block_id = kv.first;
            // TODO: Get block metadata from BlockMap and check replica_count
            // If replica_count > 1, trigger a replica downgrade
            LOG_DEBUG("HeatTracker: block {} is cold (accesses={}), consider downgrading replicas",
                      block_id, kv.second.recent_accesses);
        }
    }

} // namespace openfs