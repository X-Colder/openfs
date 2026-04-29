#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace openfs
{

    // ============================================================
    // Status codes
    // ============================================================
    enum class Status : int32_t
    {
        kOk = 0,
        kNotFound = 1,
        kAlreadyExists = 2,
        kInvalidArgument = 3,
        kIOError = 4,
        kNotDirectory = 5,
        kNotEmpty = 6,
        kNoSpace = 7,
        kCRCMismatch = 8,
        kInternal = 9,
    };

    inline const char *StatusToString(Status s)
    {
        switch (s)
        {
        case Status::kOk:
            return "OK";
        case Status::kNotFound:
            return "NotFound";
        case Status::kAlreadyExists:
            return "AlreadyExists";
        case Status::kInvalidArgument:
            return "InvalidArgument";
        case Status::kIOError:
            return "IOError";
        case Status::kNotDirectory:
            return "NotDirectory";
        case Status::kNotEmpty:
            return "NotEmpty";
        case Status::kNoSpace:
            return "NoSpace";
        case Status::kCRCMismatch:
            return "CRCMismatch";
        case Status::kInternal:
            return "Internal";
        default:
            return "Unknown";
        }
    }

    // ============================================================
    // Block levels and size constants
    // ============================================================
    enum class BlkLevel : uint8_t
    {
        L0 = 0, // 64KB
        L1 = 1, // 512KB
        L2 = 2, // 4MB
        L3 = 3, // 32MB
        L4 = 4, // 256MB
    };

    // Block size for each level
    inline constexpr uint32_t BlockLevelSize(BlkLevel level)
    {
        switch (level)
        {
        case BlkLevel::L0:
            return 64u * 1024; // 64KB
        case BlkLevel::L1:
            return 512u * 1024; // 512KB
        case BlkLevel::L2:
            return 4u * 1024 * 1024; // 4MB
        case BlkLevel::L3:
            return 32u * 1024 * 1024; // 32MB
        case BlkLevel::L4:
            return 256u * 1024 * 1024; // 256MB
        default:
            return 4u * 1024 * 1024; // default 4MB
        }
    }

    // Auto-select block level based on file size
    inline BlkLevel SelectBlockLevel(uint64_t file_size)
    {
        if (file_size <= 64u * 1024)
            return BlkLevel::L0;
        if (file_size <= 512u * 1024)
            return BlkLevel::L1;
        if (file_size <= 4u * 1024 * 1024)
            return BlkLevel::L2;
        if (file_size <= 32u * 1024 * 1024)
            return BlkLevel::L3;
        return BlkLevel::L3; // files > 32MB: split into L3 or L4 blocks
    }

    // ============================================================
    // Segment constants (legacy, kept for compatibility)
    // ============================================================
    constexpr uint64_t kSegmentSize = 256u * 1024 * 1024; // 256MB
    constexpr uint32_t kSegmentHeaderSize = 4096;         // 4KB
    constexpr uint32_t kSegmentFooterSize = 4096;         // 4KB
    constexpr uint32_t kBlockHeaderSize = 64;             // 64B
    constexpr uint32_t kBlockAlignment = 4096;            // 4KB alignment
    constexpr char kSegmentMagic[] = "OFSSG001";
    constexpr uint8_t kSegmentMagicLen = 8;

    // ============================================================
    // Disk format constants (new block-level disk management)
    // ============================================================
    constexpr uint32_t kPhysicalBlockSize = 4096;         // 4KB physical block
    constexpr uint32_t kSuperBlockSize = 4096;            // SuperBlock occupies 1 physical block
    constexpr char kDiskMagic[] = "OFSB0001";             // Disk format magic
    constexpr uint8_t kDiskMagicLen = 8;
    constexpr uint16_t kDiskFormatVersion = 1;

    // WAL constants
    constexpr uint32_t kWalBlockSize = 4096;              // WAL entry alignment
    constexpr uint64_t kWalDefaultBlocks = 256;           // 256 * 4KB = 1MB default WAL
    constexpr uint32_t kWalEntryHeaderSize = 32;          // WAL entry header
    constexpr uint32_t kWalMagic = 0x57414C21;           // "WAL!"

    // Block on-disk header (written before each logical block's data)
    // Layout: [magic 4B][block_id 8B][level 1B][reserved 3B][data_size 4B][crc32 4B][flags 4B][reserved2 4B] = 32 bytes
    constexpr uint32_t kDiskBlockHeaderSize = 32;
    constexpr uint32_t kDiskBlockMagic = 0x4F42524B;      // "OBRK"

    // Flags for block header
    enum class BlockFlags : uint32_t
    {
        kNone = 0,
        kDeleted = 1 << 0,     // Block is logically deleted
        kPacked = 1 << 1,      // Block contains packed small files
    };

    // ============================================================
    // Disk management types
    // ============================================================
    enum class DiskState : uint8_t
    {
        kUnknown = 0,      // Not yet formatted
        kNormal = 1,       // Normal operation
        kDegraded = 2,     // IO errors detected, read-only
        kFaulted = 3,      // Too many errors, offline
    };

    struct DiskSuperBlock
    {
        char magic[8] = {};
        uint16_t version = 0;
        uint64_t disk_uuid = 0;
        uint64_t node_id = 0;
        uint32_t disk_index = 0;        // disk sequence within node
        uint32_t block_size = kPhysicalBlockSize;
        uint64_t total_blocks = 0;      // total physical blocks on device
        uint64_t bitmap_offset = 0;     // byte offset of block bitmap
        uint64_t bitmap_blocks = 0;     // number of blocks used by bitmap
        uint64_t wal_offset = 0;        // byte offset of WAL region
        uint64_t wal_blocks = 0;        // number of blocks for WAL
        uint64_t data_offset = 0;       // byte offset of data area
        uint64_t data_blocks = 0;       // number of blocks in data area
        uint64_t create_time = 0;       // format timestamp
        uint32_t checksum = 0;          // CRC32 of this superblock
        uint8_t reserved[4040] = {};    // pad to 4096 bytes total

        // Compute total structure size = 56 + 4040 = 4096
        static constexpr size_t kSerializedSize = kSuperBlockSize;
    };

    // WAL entry header (32 bytes)
    struct WalEntryHeader
    {
        uint32_t magic = kWalMagic;         // Validation marker
        uint32_t entry_size = 0;            // Total entry size including header
        uint64_t block_id = 0;              // Logical block ID being written
        uint64_t data_offset = 0;           // Byte offset on disk where block data starts
        uint32_t data_size = 0;             // Data payload size
        uint32_t crc32 = 0;                 // CRC32 of the data
        uint8_t committed = 0;              // 0 = pending, 1 = committed
        uint8_t reserved[3] = {};
    };

    // On-disk block header (32 bytes, precedes block data)
    struct DiskBlockHeader
    {
        uint32_t magic = kDiskBlockMagic;
        uint64_t block_id = 0;
        uint8_t level = 0;                  // BlkLevel cast to uint8_t
        uint8_t reserved[3] = {};
        uint32_t data_size = 0;
        uint32_t crc32 = 0;
        uint32_t flags = 0;                 // BlockFlags
        uint32_t reserved2 = 0;
    };

    // ============================================================
    // Core data structures (in-memory representations)
    // ============================================================
    enum class InodeType : uint8_t
    {
        kRegular = 0,
        kDirectory = 1,
        kSymlink = 2,
    };

    struct Inode
    {
        uint64_t inode_id = 0;
        InodeType file_type = InodeType::kRegular;
        uint32_t mode = 0644;
        uint32_t uid = 0;
        uint32_t gid = 0;
        uint64_t size = 0;
        uint64_t nlink = 1;
        uint64_t atime_ns = 0;
        uint64_t mtime_ns = 0;
        uint64_t ctime_ns = 0;
        BlkLevel block_level = BlkLevel::L2;
        bool is_packed = false;
        uint64_t parent_id = 0;
        std::string name;
    };

    struct BlockMeta
    {
        uint64_t block_id = 0;
        BlkLevel level = BlkLevel::L2;
        uint32_t size = 0;
        uint32_t crc32 = 0;
        uint64_t node_id = 0;
        uint64_t segment_id = 0;
        uint64_t offset = 0;
        uint64_t create_time = 0;
        uint16_t replica_count = 1;
        uint32_t access_count = 0;
    };

    struct DirEntry
    {
        std::string name;
        uint64_t inode_id = 0;
        InodeType file_type = InodeType::kRegular;
    };

    // Utility: current time in nanoseconds
    inline uint64_t NowNs()
    {
        auto now = std::chrono::system_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch())
                .count());
    }

} // namespace openfs