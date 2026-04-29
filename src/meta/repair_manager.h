#pragma once

#include "common/types.h"
#include "meta/block_map.h"
#include "meta/node_manager.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>

namespace openfs
{

    // Repair task for a single block
    struct RepairTask
    {
        uint64_t block_id = 0;
        uint64_t source_node_id = 0; // node with a valid replica (0 if none)
        uint64_t target_node_id = 0; // node to replicate to
        uint32_t priority = 0;       // higher = more urgent (hot blocks get higher)
        bool in_progress = false;
    };

    // Priority comparator for repair queue
    struct RepairTaskCompare
    {
        bool operator()(const RepairTask &a, const RepairTask &b) const
        {
            return a.priority < b.priority; // max-heap: highest priority first
        }
    };

    // RepairManager: handles block repair when DataNodes go offline.
    // - Prioritizes hot blocks (higher access_count)
    // - Limits repair bandwidth to configurable percentage of node capacity
    // - Supports parallel repair across multiple target nodes
    class RepairManager
    {
    public:
        RepairManager(BlockMap &block_map, NodeManager &node_mgr);
        ~RepairManager();

        // Start the repair manager background thread
        Status Start();

        // Stop the repair manager
        Status Stop();

        // Called when a node goes offline - queues its blocks for repair
        void OnNodeOffline(uint64_t node_id);

        // Called when a node comes back online - cancels pending repairs for its blocks
        void OnNodeOnline(uint64_t node_id);

        // Get current repair statistics
        struct RepairStats
        {
            uint32_t pending_count = 0;
            uint32_t in_progress_count = 0;
            uint32_t completed_count = 0;
            uint32_t failed_count = 0;
        };
        RepairStats GetStats() const;

        // Configuration
        void SetMaxRepairBandwidthMbps(uint32_t mbps) { max_bandwidth_mbps_ = mbps; }
        void SetMaxConcurrentRepairs(uint32_t count) { max_concurrent_ = count; }

    private:
        void RepairLoop();
        Status ExecuteRepair(RepairTask &task);
        uint64_t SelectTargetNode(uint64_t exclude_node_id);
        void EnqueueRepairsForNode(uint64_t node_id);

        BlockMap &block_map_;
        NodeManager &node_mgr_;

        std::priority_queue<RepairTask, std::vector<RepairTask>, RepairTaskCompare> pending_tasks_;
        std::unordered_map<uint64_t, RepairTask> in_progress_; // block_id -> task
        std::unordered_set<uint64_t> queued_blocks_;           // blocks already queued

        std::mutex mutex_;
        std::condition_variable cv_;
        std::thread repair_thread_;
        std::atomic<bool> running_{false};

        RepairStats stats_;

        // Configuration
        uint32_t max_bandwidth_mbps_ = 100; // max MB/s for repair traffic
        uint32_t max_concurrent_ = 4;       // max parallel repairs
        uint32_t bandwidth_used_mbps_ = 0;  // current repair bandwidth
    };

} // namespace openfs