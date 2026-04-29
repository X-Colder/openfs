#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "meta/node_manager.h"

using namespace openfs;

class NodeManagerTest : public ::testing::Test
{
protected:
    NodeManager node_mgr;
};

// ============================================================
// RegisterNode
// ============================================================
TEST_F(NodeManagerTest, RegisterNode_FirstNode_GetsId1)
{
    uint64_t id = node_mgr.RegisterNode("localhost:50051", 1024);
    EXPECT_EQ(id, 1u);
}

TEST_F(NodeManagerTest, RegisterNode_MultipleNodes_IncrementingIds)
{
    uint64_t id1 = node_mgr.RegisterNode("host1:50051", 1024);
    uint64_t id2 = node_mgr.RegisterNode("host2:50052", 2048);
    uint64_t id3 = node_mgr.RegisterNode("host3:50053", 4096);

    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(id2, 2u);
    EXPECT_EQ(id3, 3u);
}

TEST_F(NodeManagerTest, RegisterNode_SameAddressAllowed)
{
    // Same address registered twice should get different IDs
    uint64_t id1 = node_mgr.RegisterNode("localhost:50051", 1024);
    uint64_t id2 = node_mgr.RegisterNode("localhost:50051", 2048);
    EXPECT_NE(id1, id2);
}

// ============================================================
// UpdateHeartbeat
// ============================================================
TEST_F(NodeManagerTest, UpdateHeartbeat_Success)
{
    uint64_t id = node_mgr.RegisterNode("localhost:50051", 1024);
    // Should not crash or assert
    node_mgr.UpdateHeartbeat(id, 512);
}

TEST_F(NodeManagerTest, UpdateHeartbeat_NonexistentNode_NoCrash)
{
    // Updating heartbeat for non-existent node should not crash
    node_mgr.UpdateHeartbeat(999, 100);
}

TEST_F(NodeManagerTest, UpdateHeartbeat_MultipleUpdates)
{
    uint64_t id = node_mgr.RegisterNode("localhost:50051", 1024);
    node_mgr.UpdateHeartbeat(id, 256);
    node_mgr.UpdateHeartbeat(id, 512);
    node_mgr.UpdateHeartbeat(id, 768);
    // No assertion - just ensuring no crash/race
}

// ============================================================
// GetAnyOnlineNode
// ============================================================
TEST_F(NodeManagerTest, GetAnyOnlineNode_ReturnsRegisteredNode)
{
    node_mgr.RegisterNode("host1:50051", 1024);
    node_mgr.RegisterNode("host2:50052", 2048);

    uint64_t id = node_mgr.GetAnyOnlineNode();
    EXPECT_TRUE(id == 1 || id == 2);
}

TEST_F(NodeManagerTest, GetAnyOnlineNode_NoNodes_ReturnsZero)
{
    uint64_t id = node_mgr.GetAnyOnlineNode();
    EXPECT_EQ(id, 0u);
}

// ============================================================
// HasOnlineNodes
// ============================================================
TEST_F(NodeManagerTest, HasOnlineNodes_NoNodes_ReturnsFalse)
{
    EXPECT_FALSE(node_mgr.HasOnlineNodes());
}

TEST_F(NodeManagerTest, HasOnlineNodes_WithNodes_ReturnsTrue)
{
    node_mgr.RegisterNode("localhost:50051", 1024);
    EXPECT_TRUE(node_mgr.HasOnlineNodes());
}

TEST_F(NodeManagerTest, HasOnlineNodes_MultipleNodes_ReturnsTrue)
{
    node_mgr.RegisterNode("host1:50051", 1024);
    node_mgr.RegisterNode("host2:50052", 2048);
    EXPECT_TRUE(node_mgr.HasOnlineNodes());
}

// ============================================================
// Thread safety (basic smoke test)
// ============================================================
TEST_F(NodeManagerTest, ConcurrentAccess)
{
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([this, kOpsPerThread]()
                            {
            for (int i = 0; i < kOpsPerThread; ++i)
            {
                uint64_t id = node_mgr.RegisterNode("node", 1024);
                node_mgr.UpdateHeartbeat(id, 512);
                node_mgr.HasOnlineNodes();
                node_mgr.GetAnyOnlineNode();
            } });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // After all operations, should still have online nodes
    EXPECT_TRUE(node_mgr.HasOnlineNodes());
}