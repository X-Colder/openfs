#include "data/disk_pool.h"
#include "common/logging.h"

namespace openfs
{

    Status DiskPool::AddDisk(const std::string &path, uint64_t node_id, uint32_t disk_index)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto dm = std::make_unique<DiskManager>();
        Status s = dm->Open(path, node_id, disk_index);
        if (s != Status::kOk)
            return s;

        disks_.push_back(std::move(dm));
        LOG_INFO("Added disk {} to pool (disk_id={})", path, disks_.size() - 1);
        return Status::kOk;
    }

    Status DiskPool::AddExistingDisk(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto dm = std::make_unique<DiskManager>();
        Status s = dm->OpenExisting(path);
        if (s != Status::kOk)
            return s;

        disks_.push_back(std::move(dm));
        LOG_INFO("Added existing disk {} to pool (disk_id={})", path, disks_.size() - 1);
        return Status::kOk;
    }

    void DiskPool::CloseAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &dm : disks_)
            dm->Close();
        disks_.clear();
    }

    Status DiskPool::WriteBlock(uint64_t block_id, BlkLevel level,
                                const void *data, uint32_t data_size, uint32_t crc32,
                                uint32_t &out_disk_id, uint64_t &out_physical_offset)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (disks_.empty())
            return Status::kNoSpace;

        // Select disk with most free space
        int32_t selected = SelectDisk();
        if (selected < 0)
            return Status::kNoSpace;

        uint64_t offset = 0;
        Status s = disks_[selected]->WriteBlock(block_id, level, data, data_size, crc32, offset);
        if (s != Status::kOk)
            return s;

        out_disk_id = static_cast<uint32_t>(selected);
        out_physical_offset = offset;
        return Status::kOk;
    }

    Status DiskPool::ReadBlock(uint32_t disk_id, uint64_t physical_offset,
                               std::vector<char> &out_data, uint32_t &out_crc32,
                               uint64_t &out_block_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (disk_id >= disks_.size())
            return Status::kNotFound;

        return disks_[disk_id]->ReadBlock(physical_offset, out_data, out_crc32, out_block_id);
    }

    Status DiskPool::DeleteBlock(uint32_t disk_id, uint64_t physical_offset, uint32_t data_size)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (disk_id >= disks_.size())
            return Status::kNotFound;

        return disks_[disk_id]->DeleteBlock(physical_offset, data_size);
    }

    Status DiskPool::RecoverAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto &dm : disks_)
        {
            Status s = dm->Recover();
            if (s != Status::kOk)
            {
                LOG_ERROR("Recovery failed on disk {}", dm->GetPath());
                return s;
            }
        }
        return Status::kOk;
    }

    uint64_t DiskPool::TotalFreeSpace() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t total = 0;
        for (const auto &dm : disks_)
            total += dm->FreeSpace();
        return total;
    }

    uint64_t DiskPool::TotalDataSpace() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t total = 0;
        for (const auto &dm : disks_)
            total += dm->TotalDataSpace();
        return total;
    }

    DiskManager *DiskPool::GetDisk(uint32_t disk_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (disk_id >= disks_.size())
            return nullptr;
        return disks_[disk_id].get();
    }

    int32_t DiskPool::SelectDisk() const
    {
        if (disks_.empty())
            return -1;

        int32_t best = -1;
        uint64_t best_free = 0;
        for (int32_t i = 0; i < static_cast<int32_t>(disks_.size()); ++i)
        {
            uint64_t free_blocks = disks_[i]->FreeBlockCount();
            if (free_blocks > best_free && disks_[i]->GetState() == DiskState::kNormal)
            {
                best = i;
                best_free = free_blocks;
            }
        }
        return best;
    }

} // namespace openfs