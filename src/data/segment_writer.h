#pragma once

#include "common/types.h"
#include <string>
#include <fstream>
#include <mutex>

namespace openfs
{

    // Append-only writer for a single Segment file
    class SegmentWriter
    {
    public:
        SegmentWriter(uint64_t segment_id, const std::string &path, uint64_t max_size);
        ~SegmentWriter();

        // Append a block to the segment. Returns the offset where it was written.
        Status WriteBlock(uint64_t block_id, BlkLevel level,
                          const void *data, uint32_t data_size,
                          uint32_t crc32, uint64_t &out_offset);

        bool IsFull() const { return current_offset_ + kBlockHeaderSize + kBlockAlignment > max_size_; }
        uint64_t SegmentId() const { return segment_id_; }
        uint64_t CurrentOffset() const { return current_offset_; }

        // Seal: write footer and mark read-only
        Status Seal();

    private:
        uint64_t segment_id_;
        std::string path_;
        uint64_t max_size_;
        uint64_t current_offset_;
        std::ofstream file_;
        bool sealed_ = false;
        std::mutex mutex_;

        Status WriteHeader();
    };

} // namespace openfs