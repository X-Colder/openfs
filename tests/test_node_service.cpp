#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/create_channel.h>
#include <memory>
#include "meta/meta_service_impl.h"
#include "node_service.grpc.pb.h"

using namespace openfs;

class NodeServiceIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<grpc::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<NodeService::Stub> stub_;
    std::unique_ptr<MetaServiceImpl> meta_service_;
    NodeServiceImpl *node_service_;
    int chosen_port_ = 0;

    void SetUp() override
    {
        meta_service_ = std::make_unique<MetaServiceImpl>();
        node_service_ = new NodeServiceImpl(
            meta_service_->GetNodeManager(),
            meta_service_->GetBlockMap());

        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &chosen_port_);
        builder.RegisterService(node_service_);
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);

        std::string addr = "localhost:" + std::to_string(chosen_port_);
        channel_ = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        stub_ = NodeService::NewStub(channel_);
    }

    void TearDown() override
    {
        if (server_)
            server_->Shutdown();
        delete node_service_;
    }
};

// ============================================================
// Register
// ============================================================
TEST_F(NodeServiceIntegrationTest, Register_Success)
{
    grpc::ClientContext ctx;
    RegisterReq req;
    req.set_address("localhost:8200");
    req.set_capacity(1024 * 1024 * 1024);

    RegisterResp resp;
    auto s = stub_->Register(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.status(), 0);
    EXPECT_GT(resp.node_id(), 0u);
}

TEST_F(NodeServiceIntegrationTest, Register_MultipleNodes)
{
    uint64_t prev_id = 0;
    for (int i = 0; i < 3; ++i)
    {
        grpc::ClientContext ctx;
        RegisterReq req;
        req.set_address("host" + std::to_string(i) + ":8200");
        req.set_capacity(1024);

        RegisterResp resp;
        stub_->Register(&ctx, req, &resp);
        EXPECT_EQ(resp.status(), 0);
        EXPECT_GT(resp.node_id(), prev_id);
        prev_id = resp.node_id();
    }
}

// ============================================================
// Heartbeat
// ============================================================
TEST_F(NodeServiceIntegrationTest, Heartbeat_Success)
{
    // Register first
    grpc::ClientContext ctx1;
    RegisterReq reg_req;
    reg_req.set_address("localhost:8200");
    reg_req.set_capacity(1024);
    RegisterResp reg_resp;
    stub_->Register(&ctx1, reg_req, &reg_resp);

    // Send heartbeat
    grpc::ClientContext ctx2;
    HeartbeatReq hb_req;
    hb_req.set_node_id(reg_resp.node_id());
    auto *disk = hb_req.add_disk_usage();
    disk->set_path("/data");
    disk->set_total(1024);
    disk->set_used(512);
    hb_req.set_block_count(10);
    hb_req.set_cpu_load(0.5);

    HeartbeatResp hb_resp;
    auto s = stub_->Heartbeat(&ctx2, hb_req, &hb_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(hb_resp.status(), 0);
}

TEST_F(NodeServiceIntegrationTest, Heartbeat_UnknownNode)
{
    grpc::ClientContext ctx;
    HeartbeatReq req;
    req.set_node_id(9999);

    HeartbeatResp resp;
    auto s = stub_->Heartbeat(&ctx, req, &resp);
    // gRPC call should succeed even if node not found
    EXPECT_TRUE(s.ok());
}

// ============================================================
// ReportBlocks
// ============================================================
TEST_F(NodeServiceIntegrationTest, ReportBlocks_Success)
{
    // Register first
    grpc::ClientContext ctx1;
    RegisterReq reg_req;
    reg_req.set_address("localhost:8200");
    reg_req.set_capacity(1024);
    RegisterResp reg_resp;
    stub_->Register(&ctx1, reg_req, &reg_resp);

    // Report blocks
    grpc::ClientContext ctx2;
    ReportBlocksReq req;
    req.set_node_id(reg_resp.node_id());
    req.add_block_ids(100);
    req.add_block_ids(101);
    req.add_block_ids(102);

    ReportBlocksResp resp;
    auto s = stub_->ReportBlocks(&ctx2, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.status(), 0);
}

TEST_F(NodeServiceIntegrationTest, ReportBlocks_EmptyBlockList)
{
    grpc::ClientContext ctx1;
    RegisterReq reg_req;
    reg_req.set_address("localhost:8200");
    reg_req.set_capacity(1024);
    RegisterResp reg_resp;
    stub_->Register(&ctx1, reg_req, &reg_resp);

    grpc::ClientContext ctx2;
    ReportBlocksReq req;
    req.set_node_id(reg_resp.node_id());
    // No block IDs

    ReportBlocksResp resp;
    auto s = stub_->ReportBlocks(&ctx2, req, &resp);
    EXPECT_TRUE(s.ok());
}

// ============================================================
// Register + AllocateBlocks workflow
// ============================================================
TEST_F(NodeServiceIntegrationTest, RegisterEnablesAllocateBlocks)
{
    // Before registering, no online nodes
    EXPECT_FALSE(meta_service_->GetNodeManager().HasOnlineNodes());

    // Register
    grpc::ClientContext ctx;
    RegisterReq req;
    req.set_address("localhost:8200");
    req.set_capacity(1024 * 1024 * 1024);
    RegisterResp resp;
    stub_->Register(&ctx, req, &resp);

    // Now should have online nodes
    EXPECT_TRUE(meta_service_->GetNodeManager().HasOnlineNodes());

    // AllocateBlocks should work now
    std::vector<BlockMeta> blocks;
    EXPECT_EQ(meta_service_->GetBlockAllocator().Allocate(1, BlkLevel::L2, blocks),
              Status::kOk);
}