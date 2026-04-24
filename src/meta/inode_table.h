#pragma once

#include "common/types.h"
#include <unordered_map>
#include <mutex>
#include <optional>

namespace openfs
{

    // In-memory inode table (Phase 1: simple hashmap; Phase 3: RocksDB backend)
    class InodeTable
    {
    public:
        InodeTable();
        ~InodeTable() = default;

        Status Create(const Inode &inode);
        Status Get(uint64_t inode_id, Inode &out);
        Status Update(const Inode &inode);
        Status Delete(uint64_t inode_id);
        bool Exists(uint64_t inode_id);

        // Generate a new unique inode ID
        uint64_t AllocateInodeId();

    private:
        std::unordered_map<uint64_t, Inode> inodes_;
        std::mutex mutex_;
        uint64_t next_inode_id_ = 2; // 1 is reserved for root
    };

} // namespace openfs