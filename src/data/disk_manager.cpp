#include "data/disk_manager.h"
#include "common/logging.h"
#include "common/crc32.h"
#include <cstring>
#include <chrono>
#include <fstream>

namespace openfs
{

    // ============================================================
    // DiskFormatter
    // ============================================================

    Status DiskFormatter::Format(const std::string &path, uint64_t disk_size,
                                uint64_t wal_blocks, uint64_t node_id, uint32_t disk_index)
    {
        if (wal_blocks == 0)
            wal_blocks = kWalDefaultBlocks;

        // Calculate layout
        uint64_t total_blocks = disk_size / kPhysicalBlockSize;
        if (total_blocks < 1 + 1 + wal_blocks + 1)
        {
            LOG_ERROR("Disk too small to format: {} bytes (need at least {} blocks)",
                      disk_size, 1 + 1 + wal_blocks + 1);
            return Status::kInvalidArgument;
        }

        // SuperBlock: 1 block
        uint64_t sb_block = 0;
        uint64_t bitmap_offset = kSuperBlockSize; // block 1

        // Bitmap size: 1 bit per data block, rounded up to physical block boundary
        // We don't know data_blocks yet, so estimate: data_blocks ≈ total - 1 - bitmap_blocks - wal_blocks
        // bitmap_blocks = ceil(data_blocks / 8 / kPhysicalBlockSize)
        // Solve iteratively
        uint64_t bitmap_blocks = 1; // start estimate
        for (int i = 0; i < 10; ++i)
        {
            uint64_t data_blocks = total_blocks - 1 - bitmap_blocks - wal_blocks;
            uint64_t needed_bitmap_bytes = (data_blocks + 7) / 8;
            uint64_t needed_bitmap_blocks = (needed_bitmap_bytes + kPhysicalBlockSize - 1) / kPhysicalBlockSize;
            if (needed_bitmap_blocks == bitmap_blocks)
                break;
            bitmap_blocks = needed_bitmap_blocks;
        }

        uint64_t data_offset = bitmap_offset + bitmap_blocks * kPhysicalBlockSize;
        uint64_t wal_offset = data_offset; // WAL is stored in a separate file
        uint64_t data_blocks = total_blocks - 1 - bitmap_blocks - wal_blocks;

        // Build superblock
        DiskSuperBlock sb{};
        std::memcpy(sb.magic, kDiskMagic, kDiskMagicLen);
        sb.version = kDiskFormatVersion;
        sb.disk_uuid = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        sb.node_id = node_id;
        sb.disk_index = disk_index;
        sb.block_size = kPhysicalBlockSize;
        sb.total_blocks = total_blocks;
        sb.bitmap_offset = bitmap_offset;
        sb.bitmap_blocks = bitmap_blocks;
        sb.wal_offset = 0; // WAL stored in separate file
        sb.wal_blocks = wal_blocks;
        sb.data_offset = data_offset;
        sb.data_blocks = data_blocks;
        sb.create_time = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        // Compute checksum over everything except the checksum field itself
        sb.checksum = ComputeCRC32(&sb, offsetof(DiskSuperBlock, checksum));

        // Create and write the disk file
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to create disk file: {}", path);
                return Status::kIOError;
            }

            // Write superblock
            file.write(reinterpret_cast<const char *>(&sb), sizeof(sb));
            if (!file)
            {
                LOG_ERROR("Failed to write superblock to {}", path);
                return Status::kIOError;
            }

            // Write empty bitmap (all zeros)
            std::vector<char> zero_block(kPhysicalBlockSize, 0);
            for (uint64_t i = 0; i < bitmap_blocks; ++i)
            {
                file.write(zero_block.data(), kPhysicalBlockSize);
            }

            // Write empty data area
            for (uint64_t i = 0; i < data_blocks; ++i)
            {
                file.write(zero_block.data(), kPhysicalBlockSize);
            }

            file.flush();
        }

        // Create WAL file
        std::string wal_path = path + ".wal";
        {
            std::ofstream wal_file(wal_path, std::ios::binary | std::ios::trunc);
            if (!wal_file.is_open())
            {
                LOG_ERROR("Failed to create WAL file: {}", wal_path);
                return Status::kIOError;
            }
            std::vector<char> zero_block(kPhysicalBlockSize, 0);
            for (uint64_t i = 0; i < wal_blocks; ++i)
            {
                wal_file.write(zero_block.data(), kPhysicalBlockSize);
            }
        }

        LOG_INFO("Formatted disk {} ({} blocks, {} data blocks, {} bitmap blocks, {} WAL blocks)",
                 path, total_blocks, data_blocks, bitmap_blocks, wal_blocks);
        return Status::kOk;
    }

    Status DiskFormatter::ReadSuperBlock(const std::string &path, DiskSuperBlock &sb)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return Status::kNotFound;

        file.read(reinterpret_cast<char *>(&sb), sizeof(sb));
        if (!file)
            return Status::kIOError;

        return Status::kOk;
    }

    bool DiskFormatter::IsFormatted(const std::string &path)
    {
        DiskSuperBlock sb;
        if (ReadSuperBlock(path, sb) != Status::kOk)
            return false;
        return std::memcmp(sb.magic, kDiskMagic, kDiskMagicLen) == 0;
    }

    // ============================================================
    // DiskManager
    // ============================================================

    DiskManager::~DiskManager()
    {
        Close();
    }

    Status DiskManager::Open(const std::string &path, uint64_t node_id, uint32_t disk_index)
    {
        disk_path_ = path;

        // If already formatted, just open
        if (DiskFormatter::IsFormatted(path))
        {
            return OpenExisting(path);
        }

        // Otherwise, format with a default size (for file-based disks)
        // Determine file size or use default
        std::ifstream check(path, std::ios::binary | std::ios::ate);
        uint64_t disk_size = 0;
        if (check.is_open())
        {
            disk_size = static_cast<uint64_t>(check.tellg());
        }
        if (disk_size < kPhysicalBlockSize * 10)
        {
            // Too small or doesn't exist, use default: 16MB
            disk_size = 16ULL * 1024 * 1024;
        }

        Status s = DiskFormatter::Format(path, disk_size, kWalDefaultBlocks, node_id, disk_index);
        if (s != Status::kOk)
            return s;

        return OpenExisting(path);
    }

    Status DiskManager::OpenExisting(const std::string &path)
    {
        disk_path_ = path;

        // Read superblock
        DiskSuperBlock sb;
        Status s = DiskFormatter::ReadSuperBlock(path, sb);
        if (s != Status::kOk)
        {
            LOG_ERROR("Failed to read superblock from {}", path);
            return s;
        }

        if (!ValidateSuperBlock(sb))
        {
            LOG_ERROR("Invalid superblock in {}", path);
            return Status::kInternal;
        }

        superblock_ = sb;

        // Open the block IO engine
        s = io_.Open(path);
        if (s != Status::kOk)
            return s;

        // Load bitmap
        s = LoadBitmap();
        if (s != Status::kOk)
        {
            io_.Close();
            return s;
        }

        // Open WAL
        std::string wal_path = path + ".wal";
        s = wal_.Open(wal_path, sb.wal_blocks);
        if (s != Status::kOk)
        {
            LOG_WARN("Failed to open WAL, continuing without WAL");
        }

        state_ = DiskState::kNormal;
        LOG_INFO("Opened disk {} (data_blocks={}, free_blocks={})",
                 path, sb.data_blocks, bitmap_.FreeBlocks());
        return Status::kOk;
    }

    void DiskManager::Close()
    {
        if (state_ == DiskState::kUnknown)
            return;

        // Save bitmap before closing
        SaveBitmap();

        wal_.Close();
        io_.Close();
        state_ = DiskState::kUnknown;
        LOG_INFO("Closed disk {}", disk_path_);
    }

    Status DiskManager::WriteBlock(uint64_t block_id, BlkLevel level,
                                   const void *data, uint32_t data_size, uint32_t crc32,
                                   uint64_t &out_physical_offset)
    {
        if (state_ != DiskState::kNormal)
            return Status::kInternal;

        // Calculate physical blocks needed
        uint32_t phys_blocks = CalcPhysicalBlocks(data_size);

        // Allocate from bitmap
        uint64_t start_block = bitmap_.Allocate(phys_blocks);
        if (start_block == BlockBitmap::kInvalidBlockIndex)
        {
            LOG_ERROR("No space on disk {} for block {} (need {} physical blocks)",
                      disk_path_, block_id, phys_blocks);
            return Status::kNoSpace;
        }

        // Calculate byte offset in data area
        uint64_t data_area_offset = superblock_.data_offset + start_block * kPhysicalBlockSize;

        // WAL: log the write intent
        uint64_t wal_seq = 0;
        if (wal_.IsOpen())
        {
            wal_.AppendEntry(block_id, data_area_offset, data_size, crc32, wal_seq);
        }

        // Build on-disk block header
        DiskBlockHeader hdr{};
        hdr.magic = kDiskBlockMagic;
        hdr.block_id = block_id;
        hdr.level = static_cast<uint8_t>(level);
        hdr.data_size = data_size;
        hdr.crc32 = crc32;
        hdr.flags = 0;

        // Write header + data to disk
        std::vector<char> write_buf(phys_blocks * kPhysicalBlockSize, 0);
        std::memcpy(write_buf.data(), &hdr, sizeof(hdr));
        if (data && data_size > 0)
            std::memcpy(write_buf.data() + kDiskBlockHeaderSize, data, data_size);

        Status s = io_.WriteAt(data_area_offset, write_buf.data(),
                               static_cast<uint32_t>(write_buf.size()));
        if (s != Status::kOk)
        {
            // Rollback bitmap allocation
            bitmap_.Free(start_block, phys_blocks);
            return s;
        }

        // WAL: commit the entry
        if (wal_.IsOpen() && wal_seq > 0)
        {
            wal_.CommitEntry(wal_seq);
        }

        // Save bitmap to disk
        SaveBitmap();

        out_physical_offset = data_area_offset;
        LOG_DEBUG("Wrote block {} to disk {} at offset {} ({} phys blocks)",
                  block_id, disk_path_, data_area_offset, phys_blocks);
        return Status::kOk;
    }

    Status DiskManager::ReadBlock(uint64_t physical_offset,
                                 std::vector<char> &out_data, uint32_t &out_crc32,
                                 uint64_t &out_block_id)
    {
        if (!io_.IsOpen())
            return Status::kIOError;

        // Read the block header first
        std::vector<char> hdr_buf;
        Status s = io_.ReadAt(physical_offset, kDiskBlockHeaderSize, hdr_buf);
        if (s != Status::kOk)
            return s;

        DiskBlockHeader hdr;
        std::memcpy(&hdr, hdr_buf.data(), sizeof(hdr));

        if (hdr.magic != kDiskBlockMagic)
        {
            LOG_ERROR("Invalid block header magic at offset {} in {}",
                      physical_offset, disk_path_);
            return Status::kInternal;
        }

        // Read the data payload
        if (hdr.data_size > 0)
        {
            s = io_.ReadAt(physical_offset + kDiskBlockHeaderSize, hdr.data_size, out_data);
            if (s != Status::kOk)
                return s;
        }
        else
        {
            out_data.clear();
        }

        out_crc32 = hdr.crc32;
        out_block_id = hdr.block_id;
        return Status::kOk;
    }

    Status DiskManager::DeleteBlock(uint64_t physical_offset, uint32_t data_size)
    {
        if (state_ != DiskState::kNormal)
            return Status::kInternal;

        // Calculate which physical blocks this block occupies
        uint32_t phys_blocks = CalcPhysicalBlocks(data_size);
        uint64_t start_block = (physical_offset - superblock_.data_offset) / kPhysicalBlockSize;

        // Free the blocks in bitmap
        bitmap_.Free(start_block, phys_blocks);
        SaveBitmap();

        LOG_DEBUG("Deleted block at offset {} on disk {} (freed {} phys blocks)",
                  physical_offset, disk_path_, phys_blocks);
        return Status::kOk;
    }

    Status DiskManager::Recover()
    {
        if (!wal_.IsOpen())
            return Status::kOk;

        std::vector<WalManager::WalEntry> entries;
        Status s = wal_.Replay(entries);
        if (s != Status::kOk)
            return s;

        for (const auto &entry : entries)
        {
            if (!entry.committed)
            {
                // Uncommitted entry: the block data may be partially written.
                // Free the physical blocks it occupied.
                uint32_t phys_blocks = CalcPhysicalBlocks(entry.data_size);
                uint64_t start_block = (entry.data_offset - superblock_.data_offset) / kPhysicalBlockSize;
                bitmap_.Free(start_block, phys_blocks);
                LOG_INFO("Recovery: rolled back uncommitted block {} at offset {}",
                         entry.block_id, entry.data_offset);
            }
            // Committed entries are fine - the data is already on disk
        }

        SaveBitmap();
        wal_.Reset();
        LOG_INFO("Recovery complete on disk {}", disk_path_);
        return Status::kOk;
    }

    uint64_t DiskManager::FreeSpace() const
    {
        return bitmap_.FreeBlocks() * kPhysicalBlockSize;
    }

    uint64_t DiskManager::TotalDataSpace() const
    {
        return superblock_.data_blocks * kPhysicalBlockSize;
    }

    Status DiskManager::LoadBitmap()
    {
        uint64_t bitmap_bytes = superblock_.bitmap_blocks * kPhysicalBlockSize;
        std::vector<char> buf;
        Status s = io_.ReadAt(superblock_.bitmap_offset,
                              static_cast<uint32_t>(bitmap_bytes), buf);
        if (s != Status::kOk)
        {
            LOG_ERROR("Failed to load bitmap from {}", disk_path_);
            return s;
        }

        std::vector<uint8_t> bitmap_data(buf.begin(), buf.end());
        bitmap_.Load(bitmap_data, superblock_.data_blocks);
        return Status::kOk;
    }

    Status DiskManager::SaveBitmap()
    {
        const auto &data = bitmap_.Data();
        return io_.WriteAt(superblock_.bitmap_offset, data.data(),
                           static_cast<uint32_t>(data.size()));
    }

    uint32_t DiskManager::CalcPhysicalBlocks(uint32_t data_size) const
    {
        uint64_t total = kDiskBlockHeaderSize + data_size;
        return static_cast<uint32_t>((total + kPhysicalBlockSize - 1) / kPhysicalBlockSize);
    }

    bool DiskManager::ValidateSuperBlock(const DiskSuperBlock &sb) const
    {
        if (std::memcmp(sb.magic, kDiskMagic, kDiskMagicLen) != 0)
            return false;

        if (sb.version != kDiskFormatVersion)
            return false;

        if (sb.block_size != kPhysicalBlockSize)
            return false;

        // Verify checksum
        uint32_t calc_crc = ComputeCRC32(&sb, offsetof(DiskSuperBlock, checksum));
        if (calc_crc != sb.checksum)
        {
            LOG_ERROR("SuperBlock checksum mismatch: stored={}, calculated={}", sb.checksum, calc_crc);
            return false;
        }

        return true;
    }

} // namespace openfs