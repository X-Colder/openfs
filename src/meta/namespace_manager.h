#pragma once

#include "common/types.h"
#include "meta/inode_table.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace openfs
{

    // Manages directory tree: path resolution, mkdir, readdir, lookup, rename
    class NamespaceManager
    {
    public:
        explicit NamespaceManager(InodeTable &inode_table);
        ~NamespaceManager() = default;

        // Resolve a path to its inode
        Status Lookup(const std::string &path, Inode &out);

        // Create a directory
        Status MkDir(const std::string &path, uint32_t mode, uint32_t uid, uint32_t gid, Inode &out);

        // List directory entries
        Status ReadDir(const std::string &path, std::vector<DirEntry> &entries);

        // Remove an empty directory
        Status RmDir(const std::string &path);

        // Create a file
        Status CreateFile(const std::string &path, uint32_t mode, uint32_t uid,
                          uint32_t gid, uint64_t file_size, Inode &out);

        // Delete a file
        Status DeleteFile(const std::string &path);

        // Rename
        Status Rename(const std::string &src_path, const std::string &dst_path);

    private:
        // parent_inode_id -> [(name, child_inode_id, type)]
        std::unordered_map<uint64_t, std::vector<DirEntry>> dir_entries_;
        InodeTable &inode_table_;
        std::mutex mutex_;

        // Helper: resolve path and return parent inode + filename
        Status ResolvePath(const std::string &path, uint64_t &parent_id, std::string &filename);
    };

} // namespace openfs