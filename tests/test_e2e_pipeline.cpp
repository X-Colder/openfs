#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/create_channel.h>
#include <memory>
#include <filesystem>
#include <vector>
#include "meta/meta_service_impl.h"
#include "data/data_node.h"
#include "data/data_service_impl.h"
#include "common/crc32.h"
#include "meta_service.grpc.pb.h"
#include "data_service.grpc.pb.h"
#include "node_service.grpc.pb.h"

using namespace openfs;

// End-to-end test: simulates the full client workflow:
//   CreateFsFile → AllocateBlocks → WriteBlock → CommitBlocks → GetBlockLocations → ReadBlock
class E2EPipelineTest : public ::testing::Test
{
protected:
    // MetaNode server
    std::unique_ptr<grpc::Server> meta_server_;
    std::unique_ptr<MetaServiceImpl> meta_service_;
    NodeServiceImpl *node_service_ = nullptr;
    int meta_port_ = 0;

    // DataNode server
    std::unique_ptr<grpc::Server> data_server_;
    std::unique_ptr<DataNode> data_node_;
    std::unique_ptr<DataServiceImpl> data_service_;
    int data_port_ = 0;
    std::string data_dir_;

    // Stubs
    std::unique_ptr<MetaService::Stub> meta_stub_;
    std::unique_ptr<DataService::Stub> data_stub_;
    std::unique_ptr<NodeService::Stub> node_stub_;

    void SetUp() override
    {
        // Create temp data directory
        data_dir_ = std::filesystem::temp_directory_path().string() + "/openfs_e2e_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(data_dir_);

        // ---- Start MetaNode ----
        meta_service_ = std::make_unique<MetaServiceImpl>();
        node_service_ = new NodeServiceImpl(
            meta_service_->GetNodeManager(),
            meta_service_->GetBlockMap());

        grpc::ServerBuilder meta_builder;
        meta_builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &meta_port_);
        meta_builder.RegisterService(meta_service_.get());
        meta_builder.RegisterService(node_service_);
        meta_server_ = meta_builder.BuildAndStart();
        ASSERT_NE(meta_server_, nullptr);

        // ---- Start DataNode ----
        DataNodeConfig data_config;
        data_config.data_dir = data_dir_;
        data_config.meta_addr = "localhost:" + std::to_string(meta_port_);

        data_node_ = std::make_unique<DataNode>(data_config);
        data_service_ = std::make_unique<DataServiceImpl>(*data_node_);

        grpc::ServerBuilder data_builder;
        data_builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &data_port_);
        data_builder.RegisterService(data_service_.get());
        data_server_ = data_builder.BuildAndStart();
        ASSERT_NE(data_server_, nullptr);

        // ---- Create stubs ----
        meta_stub_ = MetaService::NewStub(
            grpc::CreateChannel("localhost:" + std::to_string(meta_port_),
                                grpc::InsecureChannelCredentials()));
        data_stub_ = DataService::NewStub(
            grpc::CreateChannel("localhost:" + std::to_string(data_port_),
                                grpc::InsecureChannelCredentials()));
        node_stub_ = NodeService::NewStub(
            grpc::CreateChannel("localhost:" + std::to_string(meta_port_),
                                grpc::InsecureChannelCredentials()));

        // Register DataNode with MetaNode
        grpc::ClientContext ctx;
        RegisterReq req;
        req.set_address("localhost:" + std::to_string(data_port_));
        req.set_capacity(1024 * 1024 * 1024);
        RegisterResp resp;
        node_stub_->Register(&ctx, req, &resp);
        ASSERT_EQ(resp.status(), 0);
    }

    void TearDown() override
    {
        if (data_server_)
            data_server_->Shutdown();
        if (meta_server_)
            meta_server_->Shutdown();
        delete node_service_;
        // Release data_node_ and service_ first to close file handles
        data_service_.reset();
        data_node_.reset();
        std::filesystem::remove_all(data_dir_);
    }
};

// ============================================================
// Full pipeline: CreateFsFile → Allocate → Write → Commit → Locate → Read
// ============================================================
TEST_F(E2EPipelineTest, FullPipeline_SingleBlock)
{
    // Step 1: CreateFsFile
    grpc::ClientContext ctx1;
    CreateFsFileReq create_req;
    create_req.set_path("/e2e_test.txt");
    create_req.set_mode(0644);
    create_req.set_file_size(4096);
    CreateFsFileResp create_resp;
    auto s = meta_stub_->CreateFsFile(&ctx1, create_req, &create_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(create_resp.status(), 0);
    uint64_t inode_id = create_resp.inode().inode_id();

    // Step 2: AllocateBlocks
    grpc::ClientContext ctx2;
    AllocBlocksReq alloc_req;
    alloc_req.set_inode_id(inode_id);
    alloc_req.set_block_count(1);
    alloc_req.set_level(BLOCK_LEVEL_L0); // L0 = 64KB
    AllocBlocksResp alloc_resp;
    s = meta_stub_->AllocateBlocks(&ctx2, alloc_req, &alloc_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(alloc_resp.status(), 0);
    ASSERT_EQ(alloc_resp.blocks_size(), 1);

    auto allocated_block = alloc_resp.blocks(0);
    uint64_t block_id = allocated_block.block_id();

    // Step 3: WriteBlock to DataNode
    std::vector<char> write_data(4096, 'E');
    uint32_t crc = ComputeCRC32(write_data.data(), 4096);

    grpc::ClientContext ctx3;
    WriteBlockReq write_req;
    write_req.set_block_id(block_id);
    write_req.set_data(std::string(write_data.begin(), write_data.end()));
    write_req.set_crc32(crc);
    WriteBlockResp write_resp;
    s = data_stub_->WriteBlock(&ctx3, write_req, &write_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(write_resp.status(), 0);

    // Step 4: CommitBlocks to MetaNode
    grpc::ClientContext ctx4;
    CommitBlocksReq commit_req;
    commit_req.set_inode_id(inode_id);
    auto *blk_proto = commit_req.add_blocks();
    blk_proto->set_block_id(block_id);
    blk_proto->set_level(BLOCK_LEVEL_L0);
    blk_proto->set_node_id(allocated_block.node_id());
    blk_proto->set_segment_id(write_resp.segment_id());
    blk_proto->set_offset(write_resp.offset());
    blk_proto->set_crc32(crc);
    CommitBlocksResp commit_resp;
    s = meta_stub_->CommitBlocks(&ctx4, commit_req, &commit_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(commit_resp.status(), 0);

    // Step 5: GetBlockLocations from MetaNode
    grpc::ClientContext ctx5;
    GetBlockLocsReq locs_req;
    locs_req.set_inode_id(inode_id);
    GetBlockLocsResp locs_resp;
    s = meta_stub_->GetBlockLocations(&ctx5, locs_req, &locs_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(locs_resp.status(), 0);
    ASSERT_EQ(locs_resp.blocks_size(), 1);

    auto located_block = locs_resp.blocks(0);
    EXPECT_EQ(located_block.block_id(), block_id);

    // Step 6: ReadBlock from DataNode
    grpc::ClientContext ctx6;
    ReadBlockReq read_req;
    read_req.set_segment_id(located_block.segment_id());
    read_req.set_offset(located_block.offset());
    ReadBlockResp read_resp;
    s = data_stub_->ReadBlock(&ctx6, read_req, &read_resp);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(read_resp.status(), 0);
    EXPECT_EQ(read_resp.data().size(), 4096u);
    EXPECT_EQ(read_resp.crc32(), crc);

    // Verify data content
    EXPECT_EQ(read_resp.data(), std::string(4096, 'E'));
}

// ============================================================
// Multi-block pipeline
// ============================================================
TEST_F(E2EPipelineTest, FullPipeline_MultipleBlocks)
{
    // Create file
    grpc::ClientContext ctx1;
    CreateFsFileReq create_req;
    create_req.set_path("/multi_block.txt");
    create_req.set_mode(0644);
    create_req.set_file_size(3 * 4 * 1024 * 1024); // 3 L2 blocks
    CreateFsFileResp create_resp;
    meta_stub_->CreateFsFile(&ctx1, create_req, &create_resp);
    uint64_t inode_id = create_resp.inode().inode_id();

    // Allocate 3 blocks
    grpc::ClientContext ctx2;
    AllocBlocksReq alloc_req;
    alloc_req.set_inode_id(inode_id);
    alloc_req.set_block_count(3);
    alloc_req.set_level(BLOCK_LEVEL_L1); // L1 = 512KB
    AllocBlocksResp alloc_resp;
    meta_stub_->AllocateBlocks(&ctx2, alloc_req, &alloc_resp);
    ASSERT_EQ(alloc_resp.blocks_size(), 3);

    // Write each block and capture segment_id/offset from data node
    std::vector<uint32_t> crcs;
    std::vector<std::pair<uint64_t, uint64_t>> write_locations; // segment_id, offset
    for (int i = 0; i < 3; ++i)
    {
        std::vector<char> data(512 * 1024, static_cast<char>('A' + i));
        uint32_t crc = ComputeCRC32(data.data(), static_cast<uint32_t>(data.size()));
        crcs.push_back(crc);

        grpc::ClientContext ctx;
        WriteBlockReq req;
        req.set_block_id(alloc_resp.blocks(i).block_id());
        req.set_data(std::string(data.begin(), data.end()));
        req.set_crc32(crc);
        WriteBlockResp resp;
        auto s = data_stub_->WriteBlock(&ctx, req, &resp);
        ASSERT_TRUE(s.ok());
        ASSERT_EQ(resp.status(), 0);
        write_locations.emplace_back(resp.segment_id(), resp.offset());
    }

    // Commit all blocks with actual write locations
    grpc::ClientContext ctx3;
    CommitBlocksReq commit_req;
    commit_req.set_inode_id(inode_id);
    for (int i = 0; i < alloc_resp.blocks_size(); ++i)
    {
        auto *blk = commit_req.add_blocks();
        blk->CopyFrom(alloc_resp.blocks(i));
        // Update with actual segment_id and offset from the write operation
        blk->set_segment_id(write_locations[i].first);
        blk->set_offset(write_locations[i].second);
        blk->set_crc32(crcs[i]);
        blk->set_size(512 * 1024);
    }
    CommitBlocksResp commit_resp;
    meta_stub_->CommitBlocks(&ctx3, commit_req, &commit_resp);
    ASSERT_EQ(commit_resp.status(), 0);

    // Get block locations
    grpc::ClientContext ctx4;
    GetBlockLocsReq locs_req;
    locs_req.set_inode_id(inode_id);
    GetBlockLocsResp locs_resp;
    meta_stub_->GetBlockLocations(&ctx4, locs_req, &locs_resp);
    ASSERT_EQ(locs_resp.blocks_size(), 3);

    // Verify each block is readable
    for (int i = 0; i < 3; ++i)
    {
        grpc::ClientContext ctx;
        ReadBlockReq req;
        req.set_segment_id(locs_resp.blocks(i).segment_id());
        req.set_offset(locs_resp.blocks(i).offset());
        ReadBlockResp resp;
        auto s = data_stub_->ReadBlock(&ctx, req, &resp);
        ASSERT_TRUE(s.ok());
        EXPECT_EQ(resp.crc32(), crcs[i]);
    }
}