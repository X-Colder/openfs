#include "common/id_generator.h"
#include <chrono>
#include <thread>
#include <new>

namespace openfs
{

    IdGenerator::IdGenerator(uint16_t node_id) : node_id_(node_id & 0x3FF) {}

    uint64_t IdGenerator::CurrentTimeMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    uint64_t IdGenerator::NextId()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t now = CurrentTimeMs();
        if (now == last_timestamp_ms_)
        {
            uint64_t seq = sequence_.fetch_add(1) & 0x1FFF; // 13 bits
            if (seq == 0)
            {
                // Sequence overflow, wait for next millisecond
                while (CurrentTimeMs() == last_timestamp_ms_)
                {
                    std::this_thread::yield();
                }
                now = CurrentTimeMs();
            }
        }
        else
        {
            sequence_.store(0);
        }
        last_timestamp_ms_ = now;

        uint64_t seq = sequence_.fetch_add(1) & 0x1FFF;
        // [timestamp 41 bits][node_id 10 bits][sequence 13 bits]
        return ((now & 0x1FFFFFFFFFF) << 23) | (static_cast<uint64_t>(node_id_) << 13) | seq;
    }

    uint64_t IdGenerator::NextSequentialId()
    {
        return sequential_counter_.fetch_add(1) + 1;
    }

    // Global singleton using static local variable (thread-safe in C++11+)
    IdGenerator &GetIdGenerator()
    {
        static IdGenerator g_id_gen(0);
        return g_id_gen;
    }

    void InitIdGenerator(uint16_t node_id)
    {
        // Use placement-new to reinitialize the singleton in-place,
        // since std::atomic members make copy-assignment deleted.
        GetIdGenerator().~IdGenerator();
        new (&GetIdGenerator()) IdGenerator(node_id);
    }

} // namespace openfs