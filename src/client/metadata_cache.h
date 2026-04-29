#pragma once

#include "common/types.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace openfs
{

    // L1 Client-side metadata cache with version+lease consistency.
    // Caches inode info, directory entries, and block locations.
    // Consistency is maintained via:
    //   - Version numbers: each cached entry carries a version
    //   - Lease/expire_time: entries expire after a configurable TTL
    //   - On miss or expiry, client re-validates with MetaNode

    // Base cache entry with version and expiry
    struct CacheEntryBase
    {
        uint64_t version = 0;
        uint64_t expire_time_ns = 0; // 0 = never expires
        bool valid = true;
    };

    // Inode cache entry
    struct InodeCacheEntry : public CacheEntryBase
    {
        Inode inode;
    };

    // Block location cache entry
    struct BlockLocCacheEntry : public CacheEntryBase
    {
        uint64_t inode_id = 0;
        std::vector<BlockMeta> blocks;
    };

    // Directory cache entry
    struct DirCacheEntry : public CacheEntryBase
    {
        std::string path;
        std::vector<DirEntry> entries;
    };

    // L1 MetadataCache: LRU cache with TTL-based expiry
    class MetadataCache
    {
    public:
        explicit MetadataCache(uint64_t max_entries = 100000,
                               uint64_t ttl_seconds = 30);
        ~MetadataCache() = default;

        // ---- Inode cache ----
        bool GetInode(uint64_t inode_id, Inode &out);
        bool GetInodeByPath(const std::string &path, Inode &out);
        void PutInode(uint64_t inode_id, const Inode &inode, uint64_t version);
        void PutInodeByPath(const std::string &path, const Inode &inode, uint64_t version);
        void InvalidateInode(uint64_t inode_id);
        void InvalidateInodeByPath(const std::string &path);

        // ---- Block location cache ----
        bool GetBlockLocations(uint64_t inode_id, std::vector<BlockMeta> &out);
        void PutBlockLocations(uint64_t inode_id, const std::vector<BlockMeta> &blocks, uint64_t version);
        void InvalidateBlockLocations(uint64_t inode_id);

        // ---- Directory cache ----
        bool GetDirEntries(const std::string &path, std::vector<DirEntry> &out);
        void PutDirEntries(const std::string &path, const std::vector<DirEntry> &entries, uint64_t version);
        void InvalidateDir(const std::string &path);

        // ---- General ----
        void Clear();
        size_t Size() const;

        // Set TTL for new entries
        void SetTtlSeconds(uint64_t seconds) { ttl_seconds_ = seconds; }

    private:
        bool IsExpired(const CacheEntryBase &entry) const;

        uint64_t max_entries_;
        uint64_t ttl_seconds_;

        // Inode caches
        std::list<std::pair<uint64_t, InodeCacheEntry>> inode_lru_;
        std::unordered_map<uint64_t, decltype(inode_lru_)::iterator> inode_index_;
        std::unordered_map<std::string, uint64_t> path_to_inode_;

        // Block location cache
        std::list<std::pair<uint64_t, BlockLocCacheEntry>> block_lru_;
        std::unordered_map<uint64_t, decltype(block_lru_)::iterator> block_index_;

        // Directory cache
        std::list<std::pair<std::string, DirCacheEntry>> dir_lru_;
        std::unordered_map<std::string, decltype(dir_lru_)::iterator> dir_index_;

        mutable std::mutex mutex_;
    };

} // namespace openfs