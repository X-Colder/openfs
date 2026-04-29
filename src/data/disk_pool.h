#pragma once

#include "common/types.h"
#include "data/disk_manager.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>

namespace openfs
{

    // DiskPool: manages multiple disks as a unified storage pool.
    // Provides block-level read/write across all disks with load balancing.
    class DiskPool
    {
    public:
        DiskPool() = default;
        ~DiskPool() = default;

        // Add a disk (file path) to the pool. Will format if not already formatted.
        // Returns the disk index within the pool.
        Status AddDisk(const std::string &path, uint64_t node_id, uint32_t disk_index);

        // Add an already-formatted disk
        Status AddExistingDisk(const std::string &path);

        // Close all disks
        void CloseAll();

        // Write a block to the pool. Selects the disk with most free space.
        // Returns disk_id and physical_offset for later retrieval.
        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size, uint32_t crc32,
                          uint32_t &out_disk_id, uint64_t &out_physical_offset);

        // Read a block from a specific disk at the given offset
        Status ReadBlock(uint32_t disk_id, uint64_t physical_offset,
                         std::vector<char> &out_data, uint32_t &out_crc32,
                         uint64_t &out_block_id);

        // Delete a block from a specific disk
        Status DeleteBlock(uint32_t disk_id, uint64_t physical_offset, uint32_t data_size);

        // Run recovery on all disks
        Status RecoverAll();

        // Get total free space across all disks
        uint64_t TotalFreeSpace() const;

        // Get total data space across all disks
        uint64_t TotalDataSpace() const;

        // Get number of disks
        size_t DiskCount() const { return disks_.size(); }

        // Get a specific disk manager
        DiskManager *GetDisk(uint32_t disk_id);

        // Select the best disk for a new write (most free space)
        int32_t SelectDisk() const;

    private:
        std::vector<std::unique_ptr<DiskManager>> disks_;
        mutable std::mutex mutex_;
    };

} // namespace openfs