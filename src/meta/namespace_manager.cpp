#include "meta/namespace_manager.h"
#include "common/logging.h"
#include <sstream>

namespace openfs
{

    NamespaceManager::NamespaceManager(InodeTable &inode_table)
        : inode_table_(inode_table)
    {
        // Initialize root directory entry list
        dir_entries_[1] = {};
    }

    // Helper: split path into parent_id + filename
    Status NamespaceManager::ResolvePath(const std::string &path,
                                         uint64_t &parent_id,
                                         std::string &filename)
    {
        if (path.empty() || path[0] != '/')
        {
            return Status::kInvalidArgument;
        }
        if (path == "/")
        {
            parent_id = 1;
            filename = "";
            return Status::kOk;
        }

        // Split path components
        std::vector<std::string> components;
        std::istringstream iss(path);
        std::string token;
        while (std::getline(iss, token, '/'))
        {
            if (!token.empty())
            {
                components.push_back(token);
            }
        }
        if (components.empty())
        {
            return Status::kInvalidArgument;
        }

        filename = components.back();

        // Walk directory tree to find parent
        uint64_t current = 1; // root inode
        for (size_t i = 0; i + 1 < components.size(); ++i)
        {
            auto dit = dir_entries_.find(current);
            if (dit == dir_entries_.end())
            {
                return Status::kNotFound;
            }
            bool found = false;
            for (const auto &entry : dit->second)
            {
                if (entry.name == components[i] && entry.file_type == InodeType::kDirectory)
                {
                    current = entry.inode_id;
                    found = true;
                    break;
                }
            }
            if (!found)
                return Status::kNotFound;
        }
        parent_id = current;
        return Status::kOk;
    }

    Status NamespaceManager::Lookup(const std::string &path, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (path == "/")
        {
            return inode_table_.Get(1, out);
        }

        uint64_t parent_id;
        std::string filename;
        Status s = ResolvePath(path, parent_id, filename);
        if (s != Status::kOk)
            return s;

        auto dit = dir_entries_.find(parent_id);
        if (dit == dir_entries_.end())
            return Status::kNotFound;

        for (const auto &entry : dit->second)
        {
            if (entry.name == filename)
            {
                return inode_table_.Get(entry.inode_id, out);
            }
        }
        return Status::kNotFound;
    }

    Status NamespaceManager::MkDir(const std::string &path, uint32_t mode,
                                   uint32_t uid, uint32_t gid, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t parent_id;
        std::string dirname;
        Status s = ResolvePath(path, parent_id, dirname);
        if (s != Status::kOk)
            return s;
        if (dirname.empty())
            return Status::kAlreadyExists; // root

        // Check if already exists
        auto dit = dir_entries_.find(parent_id);
        if (dit != dir_entries_.end())
        {
            for (const auto &e : dit->second)
            {
                if (e.name == dirname)
                    return Status::kAlreadyExists;
            }
        }

        // Create inode
        uint64_t new_id = inode_table_.AllocateInodeId();
        Inode inode;
        inode.inode_id = new_id;
        inode.file_type = InodeType::kDirectory;
        inode.mode = mode;
        inode.uid = uid;
        inode.gid = gid;
        inode.nlink = 2;
        inode.parent_id = parent_id;
        inode.name = dirname;
        inode.atime_ns = NowNs();
        inode.mtime_ns = inode.atime_ns;
        inode.ctime_ns = inode.atime_ns;

        s = inode_table_.Create(inode);
        if (s != Status::kOk)
            return s;

        // Add directory entry to parent
        dir_entries_[parent_id].push_back({dirname, new_id, InodeType::kDirectory});
        // Initialize child dir's entry list
        dir_entries_[new_id] = {};

        out = inode;
        return Status::kOk;
    }

    Status NamespaceManager::ReadDir(const std::string &path,
                                     std::vector<DirEntry> &entries)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find inode for path
        Inode dir_inode;
        if (path == "/")
        {
            Status s = inode_table_.Get(1, dir_inode);
            if (s != Status::kOk)
                return s;
        }
        else
        {
            uint64_t parent_id;
            std::string dirname;
            Status s = ResolvePath(path, parent_id, dirname);
            if (s != Status::kOk)
                return s;
            auto pit = dir_entries_.find(parent_id);
            if (pit == dir_entries_.end())
                return Status::kNotFound;
            bool found = false;
            for (const auto &e : pit->second)
            {
                if (e.name == dirname && e.file_type == InodeType::kDirectory)
                {
                    dir_inode.inode_id = e.inode_id;
                    found = true;
                    break;
                }
            }
            if (!found)
                return Status::kNotDirectory;
        }

        auto dit = dir_entries_.find(dir_inode.inode_id);
        if (dit == dir_entries_.end())
            return Status::kNotDirectory;

        entries = dit->second;
        return Status::kOk;
    }

    Status NamespaceManager::RmDir(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (path == "/")
            return Status::kInvalidArgument;

        uint64_t parent_id;
        std::string dirname;
        Status s = ResolvePath(path, parent_id, dirname);
        if (s != Status::kOk)
            return s;

        auto pit = dir_entries_.find(parent_id);
        if (pit == dir_entries_.end())
            return Status::kNotFound;

        for (auto it = pit->second.begin(); it != pit->second.end(); ++it)
        {
            if (it->name == dirname && it->file_type == InodeType::kDirectory)
            {
                // Check empty
                auto cit = dir_entries_.find(it->inode_id);
                if (cit != dir_entries_.end() && !cit->second.empty())
                {
                    return Status::kNotEmpty;
                }
                uint64_t rm_id = it->inode_id;
                pit->second.erase(it);
                dir_entries_.erase(rm_id);
                inode_table_.Delete(rm_id);
                return Status::kOk;
            }
        }
        return Status::kNotFound;
    }

    Status NamespaceManager::CreateFile(const std::string &path, uint32_t mode,
                                        uint32_t uid, uint32_t gid,
                                        uint64_t file_size, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t parent_id;
        std::string filename;
        Status s = ResolvePath(path, parent_id, filename);
        if (s != Status::kOk)
            return s;

        auto dit = dir_entries_.find(parent_id);
        if (dit == dir_entries_.end())
            return Status::kNotFound;
        for (const auto &e : dit->second)
        {
            if (e.name == filename)
                return Status::kAlreadyExists;
        }

        uint64_t new_id = inode_table_.AllocateInodeId();
        Inode inode;
        inode.inode_id = new_id;
        inode.file_type = InodeType::kRegular;
        inode.mode = mode;
        inode.uid = uid;
        inode.gid = gid;
        inode.size = file_size;
        inode.nlink = 1;
        inode.parent_id = parent_id;
        inode.name = filename;
        inode.block_level = SelectBlockLevel(file_size);
        inode.atime_ns = NowNs();
        inode.mtime_ns = inode.atime_ns;
        inode.ctime_ns = inode.atime_ns;

        s = inode_table_.Create(inode);
        if (s != Status::kOk)
            return s;

        dit->second.push_back({filename, new_id, InodeType::kRegular});
        out = inode;
        return Status::kOk;
    }

    Status NamespaceManager::DeleteFile(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t parent_id;
        std::string filename;
        Status s = ResolvePath(path, parent_id, filename);
        if (s != Status::kOk)
            return s;

        auto dit = dir_entries_.find(parent_id);
        if (dit == dir_entries_.end())
            return Status::kNotFound;

        for (auto it = dit->second.begin(); it != dit->second.end(); ++it)
        {
            if (it->name == filename && it->file_type == InodeType::kRegular)
            {
                uint64_t rm_id = it->inode_id;
                dit->second.erase(it);
                inode_table_.Delete(rm_id);
                return Status::kOk;
            }
        }
        return Status::kNotFound;
    }

    Status NamespaceManager::Rename(const std::string &src_path,
                                    const std::string &dst_path)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t src_parent, dst_parent;
        std::string src_name, dst_name;
        Status s = ResolvePath(src_path, src_parent, src_name);
        if (s != Status::kOk)
            return s;
        s = ResolvePath(dst_path, dst_parent, dst_name);
        if (s != Status::kOk)
            return s;

        auto spit = dir_entries_.find(src_parent);
        if (spit == dir_entries_.end())
            return Status::kNotFound;

        DirEntry moving;
        bool found = false;
        for (auto it = spit->second.begin(); it != spit->second.end(); ++it)
        {
            if (it->name == src_name)
            {
                moving = *it;
                spit->second.erase(it);
                found = true;
                break;
            }
        }
        if (!found)
            return Status::kNotFound;

        // Check dest doesn't exist
        auto dpit = dir_entries_.find(dst_parent);
        if (dpit == dir_entries_.end())
            return Status::kNotFound;
        for (const auto &e : dpit->second)
        {
            if (e.name == dst_name)
                return Status::kAlreadyExists;
        }

        moving.name = dst_name;
        dpit->second.push_back(moving);

        // Update inode name
        Inode inode;
        if (inode_table_.Get(moving.inode_id, inode) == Status::kOk)
        {
            inode.name = dst_name;
            inode.parent_id = dst_parent;
            inode.ctime_ns = NowNs();
            inode_table_.Update(inode);
        }

        return Status::kOk;
    }

} // namespace openfs