#include "data/segment_writer.h"
#include "common/logging.h"
#include "common/crc32.h"
#include <cstring>

namespace openfs
{

    SegmentWriter::SegmentWriter(uint64_t segment_id, const std::string &path, uint64_t max_size)
        : segment_id_(segment_id), path_(path), max_size_(max_size), current_offset_(0)
    {
        file_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file_.is_open())
        {
            LOG_ERROR("Failed to open segment file {}", path_);
            return;
        }
        auto s = WriteHeader();
        if (s != Status::kOk)
        {
            LOG_ERROR("Failed to write segment header to {}", path_);
        }
    }

    SegmentWriter::~SegmentWriter()
    {
        if (file_.is_open())
            file_.close();
    }

    Status SegmentWriter::WriteHeader()
    {
        // Write segment magic + header (padded to kSegmentHeaderSize)
        char header_buf[kSegmentHeaderSize] = {};
        std::memcpy(header_buf, kSegmentMagic, kSegmentMagicLen);
        // Store segment_id right after magic
        std::memcpy(header_buf + kSegmentMagicLen, &segment_id_, sizeof(segment_id_));
        file_.write(header_buf, kSegmentHeaderSize);
        if (!file_)
            return Status::kIOError;
        current_offset_ = kSegmentHeaderSize;
        return Status::kOk;
    }

    Status SegmentWriter::WriteBlock(uint64_t block_id, BlkLevel level,
                                     const void *data, uint32_t data_size,
                                     uint32_t crc32, uint64_t &out_offset)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sealed_)
            return Status::kInvalidArgument;

        // Align current offset to kBlockAlignment
        uint64_t aligned = (current_offset_ + kBlockAlignment - 1) & ~(uint64_t(kBlockAlignment) - 1);
        if (aligned + kBlockHeaderSize + data_size > max_size_ - kSegmentFooterSize)
        {
            return Status::kNoSpace;
        }

        // Pad to alignment
        if (aligned > current_offset_)
        {
            uint64_t pad = aligned - current_offset_;
            char zeros[kBlockAlignment] = {};
            file_.write(zeros, static_cast<std::streamsize>(pad));
            if (!file_)
                return Status::kIOError;
            current_offset_ = aligned;
        }

        // Build block header (64 bytes)
        char block_hdr[kBlockHeaderSize] = {};
        // [0..7] block_id, [8] level, [9..11] reserved, [12..15] data_size, [16..19] crc32
        std::memcpy(block_hdr + 0, &block_id, 8);
        uint8_t lvl = static_cast<uint8_t>(level);
        std::memcpy(block_hdr + 8, &lvl, 1);
        std::memcpy(block_hdr + 12, &data_size, 4);
        std::memcpy(block_hdr + 16, &crc32, 4);

        file_.write(block_hdr, kBlockHeaderSize);
        if (!file_)
            return Status::kIOError;

        file_.write(static_cast<const char *>(data), data_size);
        if (!file_)
            return Status::kIOError;

        out_offset = current_offset_;
        current_offset_ += kBlockHeaderSize + data_size;

        LOG_DEBUG("Wrote block {} ({}B) to segment {} at offset {}",
                  block_id, data_size, segment_id_, out_offset);
        return Status::kOk;
    }

    Status SegmentWriter::Seal()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sealed_)
            return Status::kOk;

        // Align to end boundary
        uint64_t footer_pos = max_size_ - kSegmentFooterSize;
        if (current_offset_ < footer_pos)
        {
            file_.seekp(static_cast<std::streamoff>(footer_pos));
        }

        // Write footer (magic + segment_id + total written offset)
        char footer[kSegmentFooterSize] = {};
        std::memcpy(footer, kSegmentMagic, kSegmentMagicLen);
        std::memcpy(footer + kSegmentMagicLen, &segment_id_, sizeof(segment_id_));
        std::memcpy(footer + kSegmentMagicLen + 8, &current_offset_, sizeof(current_offset_));
        file_.write(footer, kSegmentFooterSize);
        if (!file_)
            return Status::kIOError;

        file_.flush();
        sealed_ = true;

        LOG_INFO("Sealed segment {} (data up to offset {})", segment_id_, current_offset_);
        return Status::kOk;
    }

} // namespace openfs