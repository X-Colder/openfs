#pragma once

#include "common/types.h"
#include "data/block_bitmap.h"
#include "data/wal_manager.h"
#include "data/block_io_engine.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace openfs
{

    // DiskFormatter: formats a raw device/file with OpenFS disk layout.
    // Layout: [SuperBlock 4KB] [Bitmap N blocks] [WAL M blocks] [Data Area]
    class DiskFormatter
    {
    public:
        // Format a device/file with OpenFS layout.
        // disk_size: total size in bytes of the device/file
        // wal_blocks: number of physical blocks for WAL (0 = use default)
        // node_id: the node this disk belongs to
        // disk_index: sequence number of this disk within the node
        static Status Format(const std::string &path, uint64_t disk_size,
                             uint64_t wal_blocks, uint64_t node_id, uint32_t disk_index);

        // Read and validate the superblock from a device/file
        static Status ReadSuperBlock(const std::string &path, DiskSuperBlock &sb);

        // Check if a device/file is formatted with OpenFS
        static bool IsFormatted(const std::string &path);
    };

    // DiskManager: manages a single OpenFS-formatted disk.
    // Coordinates BlockBitmap, WAL, and BlockIOEngine to provide
    // block-level read/write operations with crash consistency.
    class DiskManager
    {
    public:
        DiskManager() = default;
        ~DiskManager();

        // Format and open a disk (or open an existing formatted disk)
        Status Open(const std::string &path, uint64_t node_id = 0, uint32_t disk_index = 0);

        // Open an already-formatted disk
        Status OpenExisting(const std::string &path);

        // Close the disk
        void Close();

        // Write a logical block to this disk.
        // Allocates physical blocks, writes data with header, updates bitmap and WAL.
        // Returns the physical block offset where data was written.
        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size, uint32_t crc32,
                          uint64_t &out_physical_offset);

        // Read a logical block from this disk by physical offset.
        // Reads the block header + data, verifies CRC32.
        Status ReadBlock(uint64_t physical_offset,
                         std::vector<char> &out_data, uint32_t &out_crc32,
                         uint64_t &out_block_id);

        // Delete a logical block (marks bitmap blocks as free)
        Status DeleteBlock(uint64_t physical_offset, uint32_t data_size);

        // Recover from WAL after crash
        Status Recover();

        // Get disk info
        const DiskSuperBlock &GetSuperBlock() const { return superblock_; }
        DiskState GetState() const { return state_; }
        uint64_t FreeSpace() const;
        uint64_t TotalDataSpace() const;
        uint64_t FreeBlockCount() const { return bitmap_.FreeBlocks(); }
        uint64_t AllocatedBlockCount() const { return bitmap_.AllocatedBlocks(); }

        bool IsOpen() const { return io_.IsOpen(); }
        const std::string &GetPath() const { return disk_path_; }

    private:
        std::string disk_path_;
        DiskSuperBlock superblock_{};
        BlockBitmap bitmap_;
        WalManager wal_;
        BlockIOEngine io_;
        DiskState state_ = DiskState::kUnknown;

        // Load bitmap from disk
        Status LoadBitmap();

        // Persist bitmap to disk
        Status SaveBitmap();

        // Calculate number of physical blocks needed for a logical block write
        // (header + data, aligned to physical block size)
        uint32_t CalcPhysicalBlocks(uint32_t data_size) const;

        // Validate the superblock
        bool ValidateSuperBlock(const DiskSuperBlock &sb) const;
    };

} // namespace openfs