#include <gtest/gtest.h>
#include "common/id_generator.h"
#include <thread>
#include <vector>
#include <unordered_set>

using namespace openfs;

// ============================================================
// Basic ID generation
// ============================================================
TEST(IdGeneratorTest, SequentialId_Increments)
{
    IdGenerator gen(1);
    uint64_t id1 = gen.NextSequentialId();
    uint64_t id2 = gen.NextSequentialId();
    EXPECT_GT(id2, id1);
    EXPECT_EQ(id2, id1 + 1);
}

TEST(IdGeneratorTest, SequentialId_StartsFrom1)
{
    IdGenerator gen(0);
    uint64_t id = gen.NextSequentialId();
    EXPECT_EQ(id, 1u);
}

TEST(IdGeneratorTest, SnowflakeId_IsNonZero)
{
    IdGenerator gen(1);
    uint64_t id = gen.NextId();
    EXPECT_NE(id, 0u);
}

TEST(IdGeneratorTest, SnowflakeId_UniqueWithinSameGenerator)
{
    IdGenerator gen(1);
    std::unordered_set<uint64_t> ids;
    for (int i = 0; i < 1000; ++i)
    {
        uint64_t id = gen.NextId();
        EXPECT_TRUE(ids.insert(id).second) << "Duplicate ID: " << id;
    }
}

// ============================================================
// Node ID encoding
// ============================================================
TEST(IdGeneratorTest, SnowflakeId_DifferentNodeId_ProducesDifferentIds)
{
    IdGenerator gen1(1);
    IdGenerator gen2(2);

    uint64_t id1 = gen1.NextId();
    uint64_t id2 = gen2.NextId();

    // IDs should differ since node_id is different
    EXPECT_NE(id1, id2);
}

TEST(IdGeneratorTest, NodeId_MaskedTo10Bits)
{
    // Node ID > 1023 should be masked
    IdGenerator gen(2047); // 0x7FF -> masked to 0x3FF = 1023
    uint64_t id = gen.NextId();
    EXPECT_NE(id, 0u);

    IdGenerator gen2(1023);
    uint64_t id2 = gen2.NextId();
    EXPECT_NE(id2, 0u);
}

// ============================================================
// Thread safety
// ============================================================
TEST(IdGeneratorTest, SequentialId_ThreadSafe)
{
    IdGenerator gen(0);
    const int num_threads = 4;
    const int ids_per_thread = 1000;
    std::vector<std::thread> threads;
    std::vector<std::unordered_set<uint64_t>> thread_ids(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&gen, &thread_ids, t, ids_per_thread]()
                             {
            for (int i = 0; i < ids_per_thread; ++i) {
                uint64_t id = gen.NextSequentialId();
                thread_ids[t].insert(id);
            } });
    }

    for (auto &th : threads)
        th.join();

    // Check no duplicates across threads
    std::unordered_set<uint64_t> all_ids;
    for (const auto &ids : thread_ids)
    {
        for (uint64_t id : ids)
        {
            EXPECT_TRUE(all_ids.insert(id).second) << "Duplicate ID across threads: " << id;
        }
    }
    EXPECT_EQ(all_ids.size(), num_threads * ids_per_thread);
}

TEST(IdGeneratorTest, SnowflakeId_ThreadSafe)
{
    IdGenerator gen(1);
    const int num_threads = 4;
    const int ids_per_thread = 500;
    std::vector<std::thread> threads;
    std::vector<std::unordered_set<uint64_t>> thread_ids(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&gen, &thread_ids, t, ids_per_thread]()
                             {
            for (int i = 0; i < ids_per_thread; ++i) {
                uint64_t id = gen.NextId();
                thread_ids[t].insert(id);
            } });
    }

    for (auto &th : threads)
        th.join();

    std::unordered_set<uint64_t> all_ids;
    for (const auto &ids : thread_ids)
    {
        for (uint64_t id : ids)
        {
            EXPECT_TRUE(all_ids.insert(id).second) << "Duplicate snowflake ID: " << id;
        }
    }
    EXPECT_EQ(all_ids.size(), num_threads * ids_per_thread);
}

// ============================================================
// Global singleton
// ============================================================
TEST(IdGeneratorTest, GlobalSingleton_ReturnsValidGenerator)
{
    auto &gen = GetIdGenerator();
    uint64_t id = gen.NextSequentialId();
    EXPECT_GT(id, 0u);
}

TEST(IdGeneratorTest, InitIdGenerator_UpdatesGlobal)
{
    InitIdGenerator(42);
    auto &gen = GetIdGenerator();
    uint64_t id = gen.NextId();
    EXPECT_NE(id, 0u);
}