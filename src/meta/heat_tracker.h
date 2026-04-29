#pragma once

#include "common/types.h"
#include "meta/block_map.h"
#include "meta/node_manager.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace openfs
{

    // Heat entry for a single block
    struct HeatEntry
    {
        uint64_t block_id = 0;
        uint32_t access_count = 0;     // total access count
        uint32_t recent_accesses = 0;  // accesses in current window
        uint64_t last_access_time = 0; // nanoseconds
        bool is_hot = false;           // determined by threshold
    };

    // HeatTracker: tracks block access frequency and triggers replica upgrades/downgrades.
    // - Hot blocks (access_count > threshold) get additional replicas
    // - Cold blocks (low access_count with >1 replicas) drop to single replica
    // - Runs periodic scan to apply heat-based decisions
    class HeatTracker
    {
    public:
        HeatTracker(BlockMap &block_map, NodeManager &node_mgr);
        ~HeatTracker();

        Status Start();
        Status Stop();

        // Record a block access (called on every read)
        void OnBlockAccess(uint64_t block_id);

        // Get heat info for a block
        HeatEntry GetHeatInfo(uint64_t block_id) const;

        // Get all hot blocks
        std::vector<uint64_t> GetHotBlocks() const;

        // Get all cold blocks (low access, >1 replicas)
        std::vector<uint64_t> GetColdBlocks() const;

        // Configuration
        void SetHotThreshold(uint32_t accesses) { hot_threshold_ = accesses; }
        void SetColdThreshold(uint32_t accesses) { cold_threshold_ = accesses; }
        void SetScanIntervalSeconds(uint32_t seconds) { scan_interval_sec_ = seconds; }
        void SetDecayFactor(double factor) { decay_factor_ = factor; }

    private:
        void ScanLoop();
        void ApplyHeatDecay();
        void ProcessHotBlocks();
        void ProcessColdBlocks();

        BlockMap &block_map_;
        NodeManager &node_mgr_;

        std::unordered_map<uint64_t, HeatEntry> heat_map_;
        mutable std::mutex mutex_;
        std::thread scan_thread_;
        std::atomic<bool> running_{false};

        // Configuration
        uint32_t hot_threshold_ = 10;      // accesses per window to be "hot"
        uint32_t cold_threshold_ = 2;      // accesses per window to be "cold"
        uint32_t scan_interval_sec_ = 300; // scan every 5 minutes
        double decay_factor_ = 0.5;        // decay recent_accesses by this factor each scan
    };

} // namespace openfs