#pragma once

#include "common/types.h"
#include "data/segment_writer.h"
#include "data/segment_reader.h"
#include "data/segment_sealer.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace openfs
{

    // Manages multiple segments: creates new writers, handles reads, tracks index
    class SegmentEngine
    {
    public:
        explicit SegmentEngine(const std::string &data_dir, uint64_t segment_size = kSegmentSize);
        ~SegmentEngine() = default;

        // Write a block, auto-creates new segment if current is full
        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size,
                          uint32_t crc32,
                          uint64_t &out_segment_id, uint64_t &out_offset);

        // Read a block by segment_id + offset
        Status ReadBlock(uint64_t segment_id, uint64_t offset,
                         std::vector<char> &out_data, uint32_t &out_crc32);

    private:
        std::string data_dir_;
        uint64_t segment_size_;
        std::unique_ptr<SegmentWriter> active_writer_;
        SegmentReader reader_;
        SegmentSealer sealer_;
        uint64_t next_segment_id_ = 1;
        // block_id -> (segment_id, offset) local index
        std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> block_index_;
        std::mutex mutex_;

        Status CreateNewSegment();
    };

} // namespace openfs