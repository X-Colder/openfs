#include "client/metadata_cache.h"

namespace openfs
{

    MetadataCache::MetadataCache(uint64_t max_entries, uint64_t ttl_seconds)
        : max_entries_(max_entries), ttl_seconds_(ttl_seconds)
    {
    }

    bool MetadataCache::IsExpired(const CacheEntryBase &entry) const
    {
        if (entry.expire_time_ns == 0)
            return false;
        return NowNs() > entry.expire_time_ns;
    }

    // ---- Inode cache ----

    bool MetadataCache::GetInode(uint64_t inode_id, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inode_index_.find(inode_id);
        if (it == inode_index_.end())
            return false;

        if (IsExpired(it->second->second))
        {
            // Expired - remove and return miss
            inode_lru_.erase(it->second);
            inode_index_.erase(it);
            return false;
        }

        // Move to front (most recently used)
        out = it->second->second.inode;
        inode_lru_.splice(inode_lru_.begin(), inode_lru_, it->second);
        return true;
    }

    bool MetadataCache::GetInodeByPath(const std::string &path, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = path_to_inode_.find(path);
        if (it == path_to_inode_.end())
            return false;
        return GetInode(it->second, out); // re-lookup by inode_id
    }

    void MetadataCache::PutInode(uint64_t inode_id, const Inode &inode, uint64_t version)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        InodeCacheEntry entry;
        entry.inode = inode;
        entry.version = version;
        entry.expire_time_ns = NowNs() + ttl_seconds_ * 1000000000ULL;
        entry.valid = true;

        auto it = inode_index_.find(inode_id);
        if (it != inode_index_.end())
        {
            it->second->second = entry;
            inode_lru_.splice(inode_lru_.begin(), inode_lru_, it->second);
        }
        else
        {
            inode_lru_.push_front({inode_id, entry});
            inode_index_[inode_id] = inode_lru_.begin();

            // Evict if over capacity
            while (inode_index_.size() > max_entries_)
            {
                auto &back = inode_lru_.back();
                path_to_inode_.erase(back.second.inode.name);
                inode_index_.erase(back.first);
                inode_lru_.pop_back();
            }
        }
    }

    void MetadataCache::PutInodeByPath(const std::string &path, const Inode &inode, uint64_t version)
    {
        PutInode(inode.inode_id, inode, version);
        std::lock_guard<std::mutex> lock(mutex_);
        path_to_inode_[path] = inode.inode_id;
    }

    void MetadataCache::InvalidateInode(uint64_t inode_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inode_index_.find(inode_id);
        if (it != inode_index_.end())
        {
            inode_lru_.erase(it->second);
            inode_index_.erase(it);
        }
    }

    void MetadataCache::InvalidateInodeByPath(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = path_to_inode_.find(path);
        if (it != path_to_inode_.end())
        {
            InvalidateInode(it->second);
            path_to_inode_.erase(it);
        }
    }

    // ---- Block location cache ----

    bool MetadataCache::GetBlockLocations(uint64_t inode_id, std::vector<BlockMeta> &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = block_index_.find(inode_id);
        if (it == block_index_.end())
            return false;

        if (IsExpired(it->second->second))
        {
            block_lru_.erase(it->second);
            block_index_.erase(it);
            return false;
        }

        out = it->second->second.blocks;
        block_lru_.splice(block_lru_.begin(), block_lru_, it->second);
        return true;
    }

    void MetadataCache::PutBlockLocations(uint64_t inode_id, const std::vector<BlockMeta> &blocks, uint64_t version)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        BlockLocCacheEntry entry;
        entry.inode_id = inode_id;
        entry.blocks = blocks;
        entry.version = version;
        entry.expire_time_ns = NowNs() + ttl_seconds_ * 1000000000ULL;
        entry.valid = true;

        auto it = block_index_.find(inode_id);
        if (it != block_index_.end())
        {
            it->second->second = entry;
            block_lru_.splice(block_lru_.begin(), block_lru_, it->second);
        }
        else
        {
            block_lru_.push_front({inode_id, entry});
            block_index_[inode_id] = block_lru_.begin();

            while (block_index_.size() > max_entries_)
            {
                block_index_.erase(block_lru_.back().first);
                block_lru_.pop_back();
            }
        }
    }

    void MetadataCache::InvalidateBlockLocations(uint64_t inode_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = block_index_.find(inode_id);
        if (it != block_index_.end())
        {
            block_lru_.erase(it->second);
            block_index_.erase(it);
        }
    }

    // ---- Directory cache ----

    bool MetadataCache::GetDirEntries(const std::string &path, std::vector<DirEntry> &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = dir_index_.find(path);
        if (it == dir_index_.end())
            return false;

        if (IsExpired(it->second->second))
        {
            dir_lru_.erase(it->second);
            dir_index_.erase(it);
            return false;
        }

        out = it->second->second.entries;
        dir_lru_.splice(dir_lru_.begin(), dir_lru_, it->second);
        return true;
    }

    void MetadataCache::PutDirEntries(const std::string &path, const std::vector<DirEntry> &entries, uint64_t version)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        DirCacheEntry entry;
        entry.path = path;
        entry.entries = entries;
        entry.version = version;
        entry.expire_time_ns = NowNs() + ttl_seconds_ * 1000000000ULL;
        entry.valid = true;

        auto it = dir_index_.find(path);
        if (it != dir_index_.end())
        {
            it->second->second = entry;
            dir_lru_.splice(dir_lru_.begin(), dir_lru_, it->second);
        }
        else
        {
            dir_lru_.push_front({path, entry});
            dir_index_[path] = dir_lru_.begin();

            while (dir_index_.size() > max_entries_)
            {
                dir_index_.erase(dir_lru_.back().first);
                dir_lru_.pop_back();
            }
        }
    }

    void MetadataCache::InvalidateDir(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = dir_index_.find(path);
        if (it != dir_index_.end())
        {
            dir_lru_.erase(it->second);
            dir_index_.erase(it);
        }
    }

    // ---- General ----

    void MetadataCache::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        inode_lru_.clear();
        inode_index_.clear();
        path_to_inode_.clear();
        block_lru_.clear();
        block_index_.clear();
        dir_lru_.clear();
        dir_index_.clear();
    }

    size_t MetadataCache::Size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return inode_index_.size() + block_index_.size() + dir_index_.size();
    }

} // namespace openfs