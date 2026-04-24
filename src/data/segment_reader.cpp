#include "data/segment_reader.h"
#include "common/logging.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace openfs
{

    SegmentReader::SegmentReader(const std::string &data_dir)
        : data_dir_(data_dir) {}

    std::string SegmentReader::SegmentPath(uint64_t segment_id) const
    {
        std::ostringstream oss;
        oss << data_dir_ << "/segment_" << std::setw(6) << std::setfill('0') << segment_id << ".dat";
        return oss.str();
    }

    Status SegmentReader::ReadBlock(uint64_t segment_id, uint64_t offset,
                                    std::vector<char> &out_data, uint32_t &out_crc32)
    {
        std::string path = SegmentPath(segment_id);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            LOG_ERROR("Segment file {} not found", path);
            return Status::kNotFound;
        }

        // Seek to block offset
        file.seekg(static_cast<std::streamoff>(offset));
        if (!file)
        {
            LOG_ERROR("Failed to seek to offset {} in segment {}", offset, segment_id);
            return Status::kIOError;
        }

        // Read block header (64 bytes)
        char block_hdr[kBlockHeaderSize];
        file.read(block_hdr, kBlockHeaderSize);
        if (!file)
        {
            LOG_ERROR("Failed to read block header at offset {} in segment {}", offset, segment_id);
            return Status::kIOError;
        }

        // Parse header: [0..7] block_id, [8] level, [12..15] data_size, [16..19] crc32
        uint64_t block_id;
        uint32_t data_size;
        std::memcpy(&block_id, block_hdr + 0, 8);
        std::memcpy(&data_size, block_hdr + 12, 4);
        std::memcpy(&out_crc32, block_hdr + 16, 4);

        // Read block data
        out_data.resize(data_size);
        file.read(out_data.data(), data_size);
        if (!file)
        {
            LOG_ERROR("Failed to read block data for block {} in segment {}", block_id, segment_id);
            return Status::kIOError;
        }

        LOG_DEBUG("Read block {} ({}B) from segment {} at offset {}", block_id, data_size, segment_id, offset);
        return Status::kOk;
    }

} // namespace openfs