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
    // Segment constants
    // ============================================================
    constexpr uint64_t kSegmentSize = 256u * 1024 * 1024; // 256MB
    constexpr uint32_t kSegmentHeaderSize = 4096;         // 4KB
    constexpr uint32_t kSegmentFooterSize = 4096;         // 4KB
    constexpr uint32_t kBlockHeaderSize = 64;             // 64B
    constexpr uint32_t kBlockAlignment = 4096;            // 4KB alignment
    constexpr char kSegmentMagic[] = "OFSSG001";
    constexpr uint8_t kSegmentMagicLen = 8;

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