#include "meta/inode_table.h"
#include "common/logging.h"

namespace openfs
{

    InodeTable::InodeTable()
    {
        // Create root inode (inode_id = 1)
        Inode root;
        root.inode_id = 1;
        root.file_type = InodeType::kDirectory;
        root.mode = 0755;
        root.nlink = 2;
        root.atime_ns = NowNs();
        root.mtime_ns = root.atime_ns;
        root.ctime_ns = root.atime_ns;
        root.name = "/";
        inodes_[1] = root;
    }

    Status InodeTable::Create(const Inode &inode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inodes_.count(inode.inode_id))
        {
            return Status::kAlreadyExists;
        }
        inodes_[inode.inode_id] = inode;
        return Status::kOk;
    }

    Status InodeTable::Get(uint64_t inode_id, Inode &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inodes_.find(inode_id);
        if (it == inodes_.end())
        {
            return Status::kNotFound;
        }
        out = it->second;
        return Status::kOk;
    }

    Status InodeTable::Update(const Inode &inode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inodes_.find(inode.inode_id);
        if (it == inodes_.end())
        {
            return Status::kNotFound;
        }
        it->second = inode;
        return Status::kOk;
    }

    Status InodeTable::Delete(uint64_t inode_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inodes_.find(inode_id);
        if (it == inodes_.end())
        {
            return Status::kNotFound;
        }
        inodes_.erase(it);
        return Status::kOk;
    }

    bool InodeTable::Exists(uint64_t inode_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return inodes_.count(inode_id) > 0;
    }

    uint64_t InodeTable::AllocateInodeId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return next_inode_id_++;
    }

} // namespace openfs