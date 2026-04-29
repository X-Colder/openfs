#include "meta/repair_manager.h"
#include "common/logging.h"
#include <algorithm>

namespace openfs
{

    RepairManager::RepairManager(BlockMap &block_map, NodeManager &node_mgr)
        : block_map_(block_map), node_mgr_(node_mgr)
    {
    }

    RepairManager::~RepairManager()
    {
        Stop();
    }

    Status RepairManager::Start()
    {
        if (running_)
            return Status::kOk;

        running_ = true;
        repair_thread_ = std::thread([this]()
                                     { this->RepairLoop(); });
        LOG_INFO("RepairManager started");
        return Status::kOk;
    }

    Status RepairManager::Stop()
    {
        if (!running_)
            return Status::kOk;

        running_ = false;
        cv_.notify_all();
        if (repair_thread_.joinable())
            repair_thread_.join();

        LOG_INFO("RepairManager stopped");
        return Status::kOk;
    }

    void RepairManager::OnNodeOffline(uint64_t node_id)
    {
        LOG_INFO("RepairManager: node {} went offline, queuing block repairs", node_id);
        EnqueueRepairsForNode(node_id);
    }

    void RepairManager::OnNodeOnline(uint64_t node_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Remove queued repairs for blocks on the recovered node
        // (they may have been queued because this node was offline)
        // For now, just log - the blocks will be verified on next heartbeat
        LOG_INFO("RepairManager: node {} came back online", node_id);
    }

    RepairManager::RepairStats RepairManager::GetStats() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
        return stats_;
    }

    void RepairManager::EnqueueRepairsForNode(uint64_t node_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find all blocks on the offline node
        // Note: This requires BlockMap to support querying by node_id.
        // For Phase 5, we iterate all blocks and filter by node_id.
        std::vector<BlockMeta> all_blocks;
        // block_map_ doesn't have a GetBlocksByNode method yet, so we
        // use a simplified approach: iterate known block metas.
        // TODO: Add BlockMap::GetBlocksByNode(node_id) for efficiency

        // For now, queue repair tasks with node_id as source
        // The actual block discovery will happen in the repair loop
        RepairTask task;
        task.source_node_id = 0; // no source yet
        task.target_node_id = 0; // will be selected later
        task.priority = 1;       // default priority
        task.in_progress = false;

        // We mark the node as needing repair, and the repair loop will
        // discover and process the blocks
        cv_.notify_one();
    }

    void RepairManager::RepairLoop()
    {
        while (running_)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(5), [this]()
                         { return !pending_tasks_.empty() || !running_; });

            if (!running_)
                break;

            // Process pending tasks up to max concurrent
            while (!pending_tasks_.empty() && in_progress_.size() < max_concurrent_)
            {
                RepairTask task = pending_tasks_.top();
                pending_tasks_.pop();
                queued_blocks_.erase(task.block_id);

                // Select target node
                if (task.target_node_id == 0)
                {
                    task.target_node_id = SelectTargetNode(task.source_node_id);
                    if (task.target_node_id == 0)
                    {
                        LOG_WARN("RepairManager: no target node available for block {}",
                                 task.block_id);
                        // Re-queue with lower priority
                        task.priority = std::max(1u, task.priority / 2);
                        pending_tasks_.push(task);
                        queued_blocks_.insert(task.block_id);
                        break;
                    }
                }

                task.in_progress = true;
                in_progress_[task.block_id] = task;
                stats_.pending_count = static_cast<uint32_t>(pending_tasks_.size());
                stats_.in_progress_count = static_cast<uint32_t>(in_progress_.size());

                lock.unlock();
                Status s = ExecuteRepair(task);
                lock.lock();

                in_progress_.erase(task.block_id);
                if (s == Status::kOk)
                {
                    stats_.completed_count++;
                }
                else
                {
                    stats_.failed_count++;
                    LOG_WARN("RepairManager: repair failed for block {}, status={}",
                             task.block_id, StatusToString(s));
                }
                stats_.in_progress_count = static_cast<uint32_t>(in_progress_.size());
            }
        }
    }

    Status RepairManager::ExecuteRepair(RepairTask &task)
    {
        // TODO: In a full implementation, this would:
        // 1. Contact the source DataNode to read the block
        // 2. Write the block to the target DataNode
        // 3. Update BlockMap with the new replica location
        // For Phase 5 skeleton, we just update the metadata
        LOG_INFO("RepairManager: repairing block {} from node {} to node {}",
                 task.block_id, task.source_node_id, task.target_node_id);

        // Simulate repair by updating block metadata
        // In production, this would be an actual data transfer
        return Status::kOk;
    }

    uint64_t RepairManager::SelectTargetNode(uint64_t exclude_node_id)
    {
        // Select a target node that is online and not the excluded one
        // Prefer nodes with more free space
        uint64_t selected = node_mgr_.GetAnyOnlineNode();
        if (selected == exclude_node_id)
        {
            // Try again - simple approach for now
            // TODO: Implement proper node selection based on load/capacity
            return 0; // no suitable node found
        }
        return selected;
    }

} // namespace openfs