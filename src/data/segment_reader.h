#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace openfs
{

    // Reads blocks from segment files by offset
    class SegmentReader
    {
    public:
        explicit SegmentReader(const std::string &data_dir);
        ~SegmentReader() = default;

        // Read a block at a given segment+offset, returns data and crc
        Status ReadBlock(uint64_t segment_id, uint64_t offset,
                         std::vector<char> &out_data, uint32_t &out_crc32);

    private:
        std::string data_dir_;

        std::string SegmentPath(uint64_t segment_id) const;
    };

} // namespace openfs