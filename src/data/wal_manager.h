#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <mutex>

namespace openfs
{

    // Write-Ahead Log manager for ensuring crash consistency.
    // Before writing a block to the data area, we first log the intent (WAL entry).
    // After the block data is written, we mark the entry as committed.
    // On recovery, uncommitted entries are rolled back, committed entries are verified.
    class WalManager
    {
    public:
        WalManager() = default;

        // Open the WAL file at the given path with specified size (in blocks)
        Status Open(const std::string &path, uint64_t wal_blocks);

        // Close the WAL file
        void Close();

        // Append a WAL entry (not yet committed).
        // Returns the WAL sequence number of this entry.
        Status AppendEntry(uint64_t block_id, uint64_t data_offset,
                           uint32_t data_size, uint32_t crc32,
                           uint64_t &out_seq);

        // Commit a WAL entry by sequence number
        Status CommitEntry(uint64_t seq);

        // Get pending (uncommitted) entries for crash recovery
        struct WalEntry
        {
            uint64_t seq = 0;
            uint64_t block_id = 0;
            uint64_t data_offset = 0;
            uint32_t data_size = 0;
            uint32_t crc32 = 0;
            bool committed = false;
        };

        // Replay the WAL: returns all entries (committed and uncommitted)
        Status Replay(std::vector<WalEntry> &entries);

        // Reset the WAL (after successful recovery, clear all entries)
        Status Reset();

        // Check if WAL is open
        bool IsOpen() const { return file_.is_open(); }

    private:
        std::string path_;
        uint64_t wal_blocks_ = 0;
        uint64_t next_seq_ = 1;
        std::fstream file_;
        std::mutex mutex_;

        // Write an entry header at a given file offset
        Status WriteEntryHeader(uint64_t file_offset, const WalEntryHeader &hdr);

        // Read an entry header at a given file offset
        Status ReadEntryHeader(uint64_t file_offset, WalEntryHeader &hdr);
    };

} // namespace openfs