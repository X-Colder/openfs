#include "data/segment_engine.h"
#include "common/logging.h"
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace openfs
{

    SegmentEngine::SegmentEngine(const std::string &data_dir, uint64_t segment_size)
        : data_dir_(data_dir), segment_size_(segment_size), reader_(data_dir)
    {
        std::filesystem::create_directories(data_dir);
    }

    Status SegmentEngine::CreateNewSegment()
    {
        uint64_t seg_id = next_segment_id_++;
        std::ostringstream oss;
        oss << data_dir_ << "/segment_" << std::setw(6) << std::setfill('0') << seg_id << ".dat";
        active_writer_ = std::make_unique<SegmentWriter>(seg_id, oss.str(), segment_size_);
        LOG_INFO("Created new segment {} at {}", seg_id, oss.str());
        return Status::kOk;
    }

    Status SegmentEngine::WriteBlock(uint64_t block_id, BlkLevel level,
                                     const void *data, uint32_t data_size,
                                     uint32_t crc32,
                                     uint64_t &out_segment_id, uint64_t &out_offset)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!active_writer_)
        {
            auto s = CreateNewSegment();
            if (s != Status::kOk)
                return s;
        }

        if (active_writer_->IsFull())
        {
            sealer_.SealSegment(*active_writer_);
            auto s = CreateNewSegment();
            if (s != Status::kOk)
                return s;
        }

        uint64_t offset = 0;
        auto s = active_writer_->WriteBlock(block_id, level, data, data_size, crc32, offset);
        if (s == Status::kNoSpace)
        {
            // Current segment full, seal and retry
            sealer_.SealSegment(*active_writer_);
            auto cs = CreateNewSegment();
            if (cs != Status::kOk)
                return cs;
            s = active_writer_->WriteBlock(block_id, level, data, data_size, crc32, offset);
        }
        if (s != Status::kOk)
            return s;

        out_segment_id = active_writer_->SegmentId();
        out_offset = offset;
        block_index_[block_id] = {out_segment_id, out_offset};
        return Status::kOk;
    }

    Status SegmentEngine::ReadBlock(uint64_t segment_id, uint64_t offset,
                                    std::vector<char> &out_data, uint32_t &out_crc32)
    {
        return reader_.ReadBlock(segment_id, offset, out_data, out_crc32);
    }

} // namespace openfs