#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <mutex>

namespace openfs
{

    // Low-level block I/O engine for reading/writing raw blocks on a device/file.
    // On Linux: uses O_DIRECT for direct I/O (bypassing page cache) when opening
    // raw devices, and fsync for data durability.
    // On other platforms: falls back to fstream-based I/O.
    class BlockIOEngine
    {
    public:
        BlockIOEngine() = default;
        ~BlockIOEngine();

        // Open a device/file for block I/O.
        // On Linux with a raw device path (/dev/xxx), uses O_DIRECT.
        // On file-based disks, uses fstream.
        Status Open(const std::string &path);

        // Close the device
        void Close();

        // Read physical blocks starting at block_index
        Status ReadBlocks(uint64_t block_index, uint32_t num_blocks,
                          std::vector<char> &out_data);

        // Write physical blocks starting at block_index
        Status WriteBlocks(uint64_t block_index, uint32_t num_blocks,
                           const void *data, uint32_t data_size);

        // Read raw bytes at a given file offset
        Status ReadAt(uint64_t offset, uint32_t size, std::vector<char> &out_data);

        // Write raw bytes at a given file offset
        Status WriteAt(uint64_t offset, const void *data, uint32_t size);

        // Flush data to persistent storage (fsync on Linux)
        Status Sync();

        // Get file/device size
        uint64_t GetFileSize() const;

        // Check if open
        bool IsOpen() const;

        // Check if using direct I/O (Linux O_DIRECT)
        bool IsDirectIO() const { return use_direct_io_; }

    private:
        std::string path_;
        mutable std::mutex mutex_;

        // fstream-based I/O (used for file-based disks on all platforms)
        mutable std::fstream file_;

        // POSIX fd-based I/O (used for raw devices on Linux)
        int fd_ = -1;
        bool use_direct_io_ = false;

        // Aligned buffer for O_DIRECT operations
        static const size_t kAlignSize = 4096;
        static void *AllocAligned(size_t size);
        static void FreeAligned(void *ptr);

        Status ReadAtDirect(uint64_t offset, uint32_t size, std::vector<char> &out_data);
        Status WriteAtDirect(uint64_t offset, const void *data, uint32_t size);
    };

} // namespace openfs