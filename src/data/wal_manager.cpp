#include "data/wal_manager.h"
#include "common/logging.h"
#include "common/crc32.h"
#include <cstring>

namespace openfs
{

    Status WalManager::Open(const std::string &path, uint64_t wal_blocks)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = path;
        wal_blocks_ = wal_blocks;
        next_seq_ = 1;

        // Open in read/write mode, create if not exists
        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!file_.is_open())
        {
            // Create new file
            file_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!file_.is_open())
            {
                LOG_ERROR("Failed to create WAL file: {}", path_);
                return Status::kIOError;
            }
            file_.close();
            file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
            if (!file_.is_open())
            {
                LOG_ERROR("Failed to reopen WAL file: {}", path_);
                return Status::kIOError;
            }
        }

        // Scan to find the next sequence number
        uint64_t max_seq = 0;
        uint64_t offset = 0;
        uint64_t wal_size = wal_blocks * kWalBlockSize;

        while (offset + kWalEntryHeaderSize <= wal_size)
        {
            WalEntryHeader hdr;
            file_.seekg(static_cast<std::streamoff>(offset));
            file_.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
            if (!file_ || hdr.magic != kWalMagic)
                break;

            if (hdr.block_id > 0 && hdr.entry_size > 0)
            {
                // Valid entry found
                max_seq = offset / kWalBlockSize + 1;
            }
            offset += std::max<uint64_t>(hdr.entry_size, kWalBlockSize);
        }

        next_seq_ = max_seq + 1;
        LOG_INFO("Opened WAL at {} (next_seq={})", path_, next_seq_);
        return Status::kOk;
    }

    void WalManager::Close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open())
            file_.close();
    }

    Status WalManager::AppendEntry(uint64_t block_id, uint64_t data_offset,
                                   uint32_t data_size, uint32_t crc32,
                                   uint64_t &out_seq)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open())
            return Status::kIOError;

        uint64_t file_offset = next_seq_ * kWalBlockSize;
        uint64_t wal_size = wal_blocks_ * kWalBlockSize;
        if (file_offset + kWalEntryHeaderSize > wal_size)
        {
            LOG_ERROR("WAL full, cannot append entry for block {}", block_id);
            return Status::kNoSpace;
        }

        WalEntryHeader hdr{};
        hdr.magic = kWalMagic;
        hdr.entry_size = kWalBlockSize;
        hdr.block_id = block_id;
        hdr.data_offset = data_offset;
        hdr.data_size = data_size;
        hdr.crc32 = crc32;
        hdr.committed = 0;

        Status s = WriteEntryHeader(file_offset, hdr);
        if (s != Status::kOk)
            return s;

        file_.flush();
        out_seq = next_seq_;
        next_seq_++;
        return Status::kOk;
    }

    Status WalManager::CommitEntry(uint64_t seq)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open())
            return Status::kIOError;

        uint64_t file_offset = seq * kWalBlockSize;
        uint64_t wal_size = wal_blocks_ * kWalBlockSize;
        if (file_offset + kWalEntryHeaderSize > wal_size)
            return Status::kNotFound;

        // Read existing header
        WalEntryHeader hdr;
        Status s = ReadEntryHeader(file_offset, hdr);
        if (s != Status::kOk)
            return s;

        if (hdr.magic != kWalMagic)
            return Status::kNotFound;

        // Mark as committed
        hdr.committed = 1;
        s = WriteEntryHeader(file_offset, hdr);
        if (s != Status::kOk)
            return s;

        file_.flush();
        return Status::kOk;
    }

    Status WalManager::Replay(std::vector<WalEntry> &entries)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.clear();

        if (!file_.is_open())
            return Status::kIOError;

        uint64_t offset = 0;
        uint64_t wal_size = wal_blocks_ * kWalBlockSize;

        while (offset + kWalEntryHeaderSize <= wal_size)
        {
            WalEntryHeader hdr;
            file_.seekg(static_cast<std::streamoff>(offset));
            file_.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));

            if (!file_ || hdr.magic != kWalMagic)
                break;

            if (hdr.block_id > 0 && hdr.entry_size > 0)
            {
                WalEntry entry;
                entry.seq = offset / kWalBlockSize + 1;
                entry.block_id = hdr.block_id;
                entry.data_offset = hdr.data_offset;
                entry.data_size = hdr.data_size;
                entry.crc32 = hdr.crc32;
                entry.committed = (hdr.committed != 0);
                entries.push_back(entry);
            }

            offset += std::max<uint64_t>(hdr.entry_size, kWalBlockSize);
        }

        LOG_INFO("WAL replay: found {} entries", entries.size());
        return Status::kOk;
    }

    Status WalManager::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open())
            return Status::kIOError;

        // Truncate and recreate
        file_.close();
        file_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file_.is_open())
            return Status::kIOError;
        file_.close();

        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!file_.is_open())
            return Status::kIOError;

        next_seq_ = 1;
        LOG_INFO("WAL reset: {}", path_);
        return Status::kOk;
    }

    Status WalManager::WriteEntryHeader(uint64_t file_offset, const WalEntryHeader &hdr)
    {
        file_.seekp(static_cast<std::streamoff>(file_offset));
        file_.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
        if (!file_)
        {
            LOG_ERROR("Failed to write WAL entry at offset {}", file_offset);
            return Status::kIOError;
        }
        return Status::kOk;
    }

    Status WalManager::ReadEntryHeader(uint64_t file_offset, WalEntryHeader &hdr)
    {
        file_.seekg(static_cast<std::streamoff>(file_offset));
        file_.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
        if (!file_)
        {
            LOG_ERROR("Failed to read WAL entry at offset {}", file_offset);
            return Status::kIOError;
        }
        return Status::kOk;
    }

} // namespace openfs