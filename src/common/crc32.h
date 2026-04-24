#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace openfs
{

    // Compute CRC32 checksum for a data buffer
    uint32_t ComputeCRC32(const void *data, size_t length);

    // Compute CRC32 for a string
    inline uint32_t ComputeCRC32(const std::string &data)
    {
        return ComputeCRC32(data.data(), data.size());
    }

    // Incremental CRC32: continue from a previous CRC value
    uint32_t UpdateCRC32(uint32_t prev_crc, const void *data, size_t length);

} // namespace openfs