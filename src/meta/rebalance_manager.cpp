#include "meta/rebalance_manager.h"
#include "common/logging.h"

namespace openfs
{

    RebalanceManager::RebalanceManager(BlockMap &block_map, NodeManager &node_mgr)
        : block_map_(block_map), node_mgr_(node_mgr)
    {
    }

    RebalanceManager::~RebalanceManager()
    {
        Stop();
    }

    Status RebalanceManager::Start()
    {
        if (running_)
            return Status::kOk;

        running_ = true;
        rebalance_thread_ = std::thread([this]()
                                        { this->RebalanceLoop(); });
        LOG_INFO("RebalanceManager started");
        return Status::kOk;
    }

    Status RebalanceManager::Stop()
    {
        if (!running_)
            return Status::kOk;

        running_ = false;
        cv_.notify_all();
        if (rebalance_thread_.joinable())
            rebalance_thread_.join();

        LOG_INFO("RebalanceManager stopped");
        return Status::kOk;
    }

    void RebalanceManager::TriggerRebalance()
    {
        triggered_ = true;
        cv_.notify_one();
    }

    void RebalanceManager::OnNodeJoined(uint64_t node_id)
    {
        LOG_INFO("RebalanceManager: node {} joined, triggering rebalance", node_id);
        TriggerRebalance();
    }

    RebalanceManager::RebalanceStats RebalanceManager::GetStats() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
        return stats_;
    }

    void RebalanceManager::RebalanceLoop()
    {
        while (running_)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(check_interval_sec_),
                         [this]()
                         { return triggered_.load() || !running_.load(); });

            if (!running_)
                break;

            if (!triggered_ && !CheckImbalance())
                continue;

            triggered_ = false;
            stats_.rebalance_in_progress = true;

            // Generate migration plan
            auto plan = GenerateMigrationPlan();
            stats_.migrations_pending = static_cast<uint32_t>(plan.size());

            lock.unlock();

            if (plan.empty())
            {
                LOG_INFO("RebalanceManager: no migrations needed");
                stats_.rebalance_in_progress = false;
                continue;
            }

            LOG_INFO("RebalanceManager: executing {} migrations", plan.size());

            // Execute migrations with rate limiting
            for (const auto &task : plan)
            {
                if (!running_)
                    break;

                Status s = ExecuteMigration(task);
                lock.lock();
                if (s == Status::kOk)
                {
                    stats_.migrations_completed++;
                }
                else
                {
                    stats_.migrations_failed++;
                }
                stats_.migrations_pending--;
                lock.unlock();

                // Rate limit: sleep between migrations
                // At max_bandwidth_mbps_ MB/s, a 4MB block takes 4/max_bandwidth_mbps_ seconds
                std::this_thread::sleep_for(
                    std::chrono::microseconds(4 * 1000000 / max_bandwidth_mbps_));
            }

            stats_.rebalance_in_progress = false;
            LOG_INFO("RebalanceManager: rebalance round completed");
        }
    }

    bool RebalanceManager::CheckImbalance()
    {
        // Check if node usage imbalance exceeds threshold
        // For now, always return false to avoid unnecessary rebalance
        // TODO: Query NodeManager for per-node usage and compute imbalance
        return false;
    }

    std::vector<MigrationTask> RebalanceManager::GenerateMigrationPlan()
    {
        // Generate a list of block migrations to balance the cluster.
        // Strategy: move blocks from high-usage nodes to low-usage nodes.
        // TODO: Implement full migration plan generation based on node usage stats
        std::vector<MigrationTask> plan;
        // Placeholder - no migrations generated yet
        return plan;
    }

    Status RebalanceManager::ExecuteMigration(const MigrationTask &task)
    {
        // TODO: In a full implementation:
        // 1. Read block from source DataNode
        // 2. Write block to target DataNode
        // 3. Update BlockMap with new location
        // 4. Delete block from source DataNode
        LOG_INFO("RebalanceManager: migrating block {} from node {} to node {}",
                 task.block_id, task.source_node_id, task.target_node_id);
        return Status::kOk;
    }

} // namespace openfs