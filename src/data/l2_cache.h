#pragma once

#include "common/types.h"
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex>
#include <cstdint>

namespace openfs
{

    // L2 DataNode DRAM Cache with LRU-2 eviction policy.
    // LRU-2: an item must be accessed at least twice before being promoted to
    // the main cache (prevents scan pollution). Items accessed once go to a
    // probationary segment; second access promotes them to the protected segment.
    class L2Cache
    {
    public:
        explicit L2Cache(uint64_t max_size_bytes);
        ~L2Cache() = default;

        // Put a block into the cache
        void Put(uint64_t block_id, const std::vector<char> &data);

        // Get a block from the cache. Returns true if hit.
        bool Get(uint64_t block_id, std::vector<char> &out_data);

        // Remove a block from the cache
        void Remove(uint64_t block_id);

        // Check if a block is in the cache
        bool Contains(uint64_t block_id) const;

        // Clear all cached data
        void Clear();

        // Get current cache size in bytes
        uint64_t Size() const;

        // Get max cache size in bytes
        uint64_t Capacity() const { return max_size_bytes_; }

        // Get number of cached blocks
        size_t Count() const;

        // Get hit/miss statistics
        struct CacheStats
        {
            uint64_t hits = 0;
            uint64_t misses = 0;
            uint64_t evictions = 0;
            double HitRate() const
            {
                uint64_t total = hits + misses;
                return total > 0 ? static_cast<double>(hits) / total : 0.0;
            }
        };
        CacheStats GetStats() const;

    private:
        // Cache entry
        struct CacheEntry
        {
            uint64_t block_id;
            std::vector<char> data;
            int access_count; // 1 = probationary, 2+ = protected
        };

        // LRU list type: most recently used at front
        using LRUList = std::list<CacheEntry>;
        using LRUIterator = LRUList::iterator;

        void EvictIfNeeded();
        void MoveToProtected(LRUIterator it);

        uint64_t max_size_bytes_;
        uint64_t current_size_bytes_ = 0;

        // Probationary segment: items accessed once
        LRUList probationary_;
        // Protected segment: items accessed 2+ times
        LRUList protected_;

        // Index: block_id -> iterator (into either list)
        std::unordered_map<uint64_t, LRUIterator> index_;

        mutable std::mutex mutex_;
        mutable CacheStats stats_;
    };

} // namespace openfs