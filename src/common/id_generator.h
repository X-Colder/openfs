#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace openfs
{

    // Thread-safe unique ID generator using Snowflake-like approach.
    // Format: [timestamp_ms 41 bits][node_id 10 bits][sequence 13 bits]
    class IdGenerator
    {
    public:
        // node_id: 0-1023, identifies the meta node in the cluster
        explicit IdGenerator(uint16_t node_id = 0);

        // Generate a globally unique ID
        uint64_t NextId();

        // Generate a simple sequential ID (for single-node mode)
        uint64_t NextSequentialId();

    private:
        uint16_t node_id_;
        std::atomic<uint64_t> sequence_{0};
        std::atomic<uint64_t> sequential_counter_{0};
        uint64_t last_timestamp_ms_{0};
        std::mutex mutex_;

        static uint64_t CurrentTimeMs();
    };

    // Global singleton for convenience
    IdGenerator &GetIdGenerator();

    // Initialize the global generator with a specific node_id
    void InitIdGenerator(uint16_t node_id);

} // namespace openfs