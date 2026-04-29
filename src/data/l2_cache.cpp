#include "data/l2_cache.h"
#include "common/logging.h"
#include <algorithm>

namespace openfs
{

    L2Cache::L2Cache(uint64_t max_size_bytes)
        : max_size_bytes_(max_size_bytes)
    {
    }

    void L2Cache::Put(uint64_t block_id, const std::vector<char> &data)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If already cached, just update
        auto it = index_.find(block_id);
        if (it != index_.end())
        {
            auto &entry = *(it->second);
            current_size_bytes_ -= entry.data.size();
            entry.data = data;
            entry.access_count++;
            current_size_bytes_ += data.size();

            // Promote to protected if accessed 2+ times
            if (entry.access_count >= 2)
                MoveToProtected(it->second);
            return;
        }

        // Create new entry in probationary list (first access)
        CacheEntry entry;
        entry.block_id = block_id;
        entry.data = data;
        entry.access_count = 1;

        probationary_.push_front(std::move(entry));
        index_[block_id] = probationary_.begin();
        current_size_bytes_ += data.size();

        EvictIfNeeded();
    }

    bool L2Cache::Get(uint64_t block_id, std::vector<char> &out_data)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(block_id);
        if (it == index_.end())
        {
            stats_.misses++;
            return false;
        }

        auto &entry = *(it->second);
        entry.access_count++;

        // Promote to protected if accessed 2+ times
        if (entry.access_count >= 2)
            MoveToProtected(it->second);

        out_data = entry.data;
        stats_.hits++;
        return true;
    }

    void L2Cache::Remove(uint64_t block_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(block_id);
        if (it == index_.end())
            return;

        current_size_bytes_ -= it->second->data.size();
        it->second->access_count = 0; // mark for removal
        // Erase from the list
        if (it->second->access_count <= 1)
            probationary_.erase(it->second);
        else
            protected_.erase(it->second);
        index_.erase(it);
    }

    bool L2Cache::Contains(uint64_t block_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.find(block_id) != index_.end();
    }

    void L2Cache::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        probationary_.clear();
        protected_.clear();
        index_.clear();
        current_size_bytes_ = 0;
    }

    uint64_t L2Cache::Size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_size_bytes_;
    }

    size_t L2Cache::Count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.size();
    }

    L2Cache::CacheStats L2Cache::GetStats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void L2Cache::EvictIfNeeded()
    {
        // Evict from probationary first, then protected
        while (current_size_bytes_ > max_size_bytes_)
        {
            if (!probationary_.empty())
            {
                // Evict from back of probationary (least recently used)
                auto &entry = probationary_.back();
                current_size_bytes_ -= entry.data.size();
                index_.erase(entry.block_id);
                probationary_.pop_back();
                stats_.evictions++;
            }
            else if (!protected_.empty())
            {
                // Evict from back of protected (least recently used)
                auto &entry = protected_.back();
                current_size_bytes_ -= entry.data.size();
                index_.erase(entry.block_id);
                protected_.pop_back();
                stats_.evictions++;
            }
            else
            {
                break; // nothing to evict
            }
        }
    }

    void L2Cache::MoveToProtected(LRUIterator it)
    {
        // Determine which list the iterator belongs to
        // We move it to the front of the protected list
        CacheEntry entry = std::move(*it);

        // Remove from current list
        // Check if it's in probationary or protected by scanning
        for (auto pit = probationary_.begin(); pit != probationary_.end(); ++pit)
        {
            if (pit->block_id == entry.block_id)
            {
                probationary_.erase(pit);
                break;
            }
        }
        for (auto pit = protected_.begin(); pit != protected_.end(); ++pit)
        {
            if (pit->block_id == entry.block_id)
            {
                protected_.erase(pit);
                break;
            }
        }

        // Insert at front of protected
        protected_.push_front(std::move(entry));
        index_[protected_.front().block_id] = protected_.begin();
    }

} // namespace openfs