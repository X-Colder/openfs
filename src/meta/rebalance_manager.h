#pragma once

#include "common/types.h"
#include "meta/block_map.h"
#include "meta/node_manager.h"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace openfs
{

    // Migration task: move a block from one node to another
    struct MigrationTask
    {
        uint64_t block_id = 0;
        uint64_t source_node_id = 0;
        uint64_t target_node_id = 0;
    };

    // RebalanceManager: rebalances block distribution across DataNodes.
    // Triggered when:
    //   - New node joins the cluster
    //   - Node usage imbalance exceeds threshold (default 20%)
    // Limits migration bandwidth to avoid impacting online traffic.
    class RebalanceManager
    {
    public:
        RebalanceManager(BlockMap &block_map, NodeManager &node_mgr);
        ~RebalanceManager();

        Status Start();
        Status Stop();

        // Trigger rebalance check (e.g., after new node joins)
        void TriggerRebalance();

        // Called when a new node joins
        void OnNodeJoined(uint64_t node_id);

        // Get statistics
        struct RebalanceStats
        {
            uint32_t migrations_completed = 0;
            uint32_t migrations_failed = 0;
            uint32_t migrations_pending = 0;
            bool rebalance_in_progress = false;
        };
        RebalanceStats GetStats() const;

        // Configuration
        void SetImbalanceThreshold(double threshold) { imbalance_threshold_ = threshold; }
        void SetMaxBandwidthMbps(uint32_t mbps) { max_bandwidth_mbps_ = mbps; }
        void SetCheckIntervalSeconds(uint32_t seconds) { check_interval_sec_ = seconds; }

    private:
        void RebalanceLoop();
        bool CheckImbalance();
        std::vector<MigrationTask> GenerateMigrationPlan();
        Status ExecuteMigration(const MigrationTask &task);

        BlockMap &block_map_;
        NodeManager &node_mgr_;

        std::vector<MigrationTask> pending_migrations_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::thread rebalance_thread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> triggered_{false};

        RebalanceStats stats_;

        // Configuration
        double imbalance_threshold_ = 0.20; // 20% usage deviation triggers rebalance
        uint32_t max_bandwidth_mbps_ = 50;  // max MB/s for migration traffic
        uint32_t check_interval_sec_ = 60;  // check every 60 seconds
    };

} // namespace openfs