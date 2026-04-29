#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/create_channel.h>
#include <memory>
#include <thread>
#include <filesystem>
#include "meta/meta_service_impl.h"
#include "meta_service.grpc.pb.h"

using namespace openfs;

class MetaServiceIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<grpc::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<MetaService::Stub> stub_;
    std::unique_ptr<MetaServiceImpl> service_;
    std::string server_addr_;

    void SetUp() override
    {
        // Find a free port
        server_addr_ = "localhost:0";
        service_ = std::make_unique<MetaServiceImpl>();

        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &chosen_port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);

        server_addr_ = "localhost:" + std::to_string(chosen_port_);
        channel_ = grpc::CreateChannel(server_addr_, grpc::InsecureChannelCredentials());
        stub_ = MetaService::NewStub(channel_);

        // Register a DataNode so AllocateBlocks can work
        service_->GetNodeManager().RegisterNode("localhost:50051", 1024 * 1024 * 1024);
    }

    void TearDown() override
    {
        if (server_)
        {
            server_->Shutdown();
        }
    }

    int chosen_port_ = 0;
};

// ============================================================
// CreateFsFile
// ============================================================
TEST_F(MetaServiceIntegrationTest, CreateFsFile_Success)
{
    grpc::ClientContext ctx;
    CreateFsFileReq req;
    req.set_path("/test.txt");
    req.set_mode(0644);
    req.set_uid(0);
    req.set_gid(0);
    req.set_file_size(1024);

    CreateFsFileResp resp;
    auto status = stub_->CreateFsFile(&ctx, req, &resp);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(resp.status(), 0);
    EXPECT_TRUE(resp.has_inode());
    EXPECT_EQ(resp.inode().name(), "test.txt");
    EXPECT_EQ(resp.inode().file_type(), FILE_TYPE_REGULAR);
}

TEST_F(MetaServiceIntegrationTest, CreateFsFile_DuplicatePath)
{
    grpc::ClientContext ctx1, ctx2;
    CreateFsFileReq req;
    req.set_path("/dup.txt");
    req.set_mode(0644);

    CreateFsFileResp resp1, resp2;
    stub_->CreateFsFile(&ctx1, req, &resp1);
    EXPECT_EQ(resp1.status(), 0);

    auto status = stub_->CreateFsFile(&ctx2, req, &resp2);
    EXPECT_TRUE(status.ok());
    EXPECT_NE(resp2.status(), 0); // Should not be OK
}

TEST_F(MetaServiceIntegrationTest, CreateFsFile_InSubdirectory)
{
    // Create directory first
    grpc::ClientContext ctx1;
    MkDirReq mkdir_req;
    mkdir_req.set_path("/data");
    mkdir_req.set_mode(0755);
    MkDirResp mkdir_resp;
    stub_->MkDir(&ctx1, mkdir_req, &mkdir_resp);
    ASSERT_EQ(mkdir_resp.status(), 0);

    // Create file in directory
    grpc::ClientContext ctx2;
    CreateFsFileReq file_req;
    file_req.set_path("/data/file.txt");
    file_req.set_mode(0644);
    file_req.set_file_size(4096);
    CreateFsFileResp file_resp;
    auto s = stub_->CreateFsFile(&ctx2, file_req, &file_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(file_resp.status(), 0);
}

// ============================================================
// GetFileInfo
// ============================================================
TEST_F(MetaServiceIntegrationTest, GetFileInfo_Success)
{
    // Create file first
    grpc::ClientContext ctx1;
    CreateFsFileReq req;
    req.set_path("/info_test.txt");
    req.set_mode(0644);
    req.set_file_size(2048);
    CreateFsFileResp create_resp;
    stub_->CreateFsFile(&ctx1, req, &create_resp);

    // Get file info
    grpc::ClientContext ctx2;
    GetFileInfoReq info_req;
    info_req.set_path("/info_test.txt");
    GetFileInfoResp info_resp;
    auto s = stub_->GetFileInfo(&ctx2, info_req, &info_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(info_resp.status(), 0);
    EXPECT_EQ(info_resp.inode().size(), 2048u);
}

TEST_F(MetaServiceIntegrationTest, GetFileInfo_NotFound)
{
    grpc::ClientContext ctx;
    GetFileInfoReq req;
    req.set_path("/nonexistent.txt");
    GetFileInfoResp resp;
    auto s = stub_->GetFileInfo(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_NE(resp.status(), 0);
}

// ============================================================
// MkDir
// ============================================================
TEST_F(MetaServiceIntegrationTest, MkDir_Success)
{
    grpc::ClientContext ctx;
    MkDirReq req;
    req.set_path("/new_dir");
    req.set_mode(0755);
    MkDirResp resp;
    auto s = stub_->MkDir(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.status(), 0);
    EXPECT_TRUE(resp.has_inode());
    EXPECT_EQ(resp.inode().file_type(), FILE_TYPE_DIRECTORY);
}

// ============================================================
// ReadDir
// ============================================================
TEST_F(MetaServiceIntegrationTest, ReadDir_Success)
{
    // Create a few files
    for (int i = 0; i < 3; ++i)
    {
        grpc::ClientContext ctx;
        CreateFsFileReq req;
        req.set_path("/file" + std::to_string(i) + ".txt");
        req.set_mode(0644);
        CreateFsFileResp resp;
        stub_->CreateFsFile(&ctx, req, &resp);
    }

    grpc::ClientContext ctx;
    ReadDirReq req;
    req.set_path("/");
    ReadDirResp resp;
    auto s = stub_->ReadDir(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.status(), 0);
    EXPECT_GE(resp.entries_size(), 3);
}

// ============================================================
// RemoveFsFile
// ============================================================
TEST_F(MetaServiceIntegrationTest, RemoveFsFile_Success)
{
    // Create file
    grpc::ClientContext ctx1;
    CreateFsFileReq req;
    req.set_path("/to_delete.txt");
    req.set_mode(0644);
    CreateFsFileResp create_resp;
    stub_->CreateFsFile(&ctx1, req, &create_resp);

    // Delete it
    grpc::ClientContext ctx2;
    RemoveFsFileReq del_req;
    del_req.set_path("/to_delete.txt");
    RemoveFsFileResp del_resp;
    auto s = stub_->RemoveFsFile(&ctx2, del_req, &del_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(del_resp.status(), 0);

    // Verify it's gone
    grpc::ClientContext ctx3;
    GetFileInfoReq info_req;
    info_req.set_path("/to_delete.txt");
    GetFileInfoResp info_resp;
    stub_->GetFileInfo(&ctx3, info_req, &info_resp);
    EXPECT_NE(info_resp.status(), 0);
}

// ============================================================
// AllocateBlocks + GetBlockLocations
// ============================================================
TEST_F(MetaServiceIntegrationTest, AllocateBlocks_Success)
{
    // Create file first
    grpc::ClientContext ctx1;
    CreateFsFileReq req;
    req.set_path("/alloc_test.txt");
    req.set_mode(0644);
    req.set_file_size(4 * 1024 * 1024);
    CreateFsFileResp create_resp;
    stub_->CreateFsFile(&ctx1, req, &create_resp);
    uint64_t inode_id = create_resp.inode().inode_id();

    // Allocate blocks
    grpc::ClientContext ctx2;
    AllocBlocksReq alloc_req;
    alloc_req.set_inode_id(inode_id);
    alloc_req.set_block_count(2);
    alloc_req.set_level(BLOCK_LEVEL_L2);
    AllocBlocksResp alloc_resp;
    auto s = stub_->AllocateBlocks(&ctx2, alloc_req, &alloc_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(alloc_resp.status(), 0);
    EXPECT_EQ(alloc_resp.blocks_size(), 2);

    for (const auto &blk : alloc_resp.blocks())
    {
        EXPECT_GT(blk.block_id(), 0u);
        EXPECT_GT(blk.node_id(), 0u);
        EXPECT_GT(blk.segment_id(), 0u);
    }
}

TEST_F(MetaServiceIntegrationTest, GetBlockLocations_AfterCommit)
{
    // Create file
    grpc::ClientContext ctx0;
    CreateFsFileReq req;
    req.set_path("/commit_test.txt");
    req.set_mode(0644);
    req.set_file_size(1024);
    CreateFsFileResp create_resp;
    stub_->CreateFsFile(&ctx0, req, &create_resp);
    uint64_t inode_id = create_resp.inode().inode_id();

    // Allocate
    grpc::ClientContext ctx1;
    AllocBlocksReq alloc_req;
    alloc_req.set_inode_id(inode_id);
    alloc_req.set_block_count(1);
    alloc_req.set_level(BLOCK_LEVEL_L0);
    AllocBlocksResp alloc_resp;
    stub_->AllocateBlocks(&ctx1, alloc_req, &alloc_resp);
    ASSERT_EQ(alloc_resp.blocks_size(), 1);

    // Commit
    grpc::ClientContext ctx2;
    CommitBlocksReq commit_req;
    commit_req.set_inode_id(inode_id);
    auto *blk = commit_req.add_blocks();
    blk->CopyFrom(alloc_resp.blocks(0));
    CommitBlocksResp commit_resp;
    auto s = stub_->CommitBlocks(&ctx2, commit_req, &commit_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(commit_resp.status(), 0);

    // Get block locations
    grpc::ClientContext ctx3;
    GetBlockLocsReq locs_req;
    locs_req.set_inode_id(inode_id);
    GetBlockLocsResp locs_resp;
    s = stub_->GetBlockLocations(&ctx3, locs_req, &locs_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(locs_resp.status(), 0);
    EXPECT_EQ(locs_resp.blocks_size(), 1);
}

// ============================================================
// RmDir
// ============================================================
TEST_F(MetaServiceIntegrationTest, RmDir_EmptyDir_Success)
{
    grpc::ClientContext ctx1;
    MkDirReq mkdir_req;
    mkdir_req.set_path("/empty_dir");
    mkdir_req.set_mode(0755);
    MkDirResp mkdir_resp;
    stub_->MkDir(&ctx1, mkdir_req, &mkdir_resp);

    grpc::ClientContext ctx2;
    RmDirReq rmdir_req;
    rmdir_req.set_path("/empty_dir");
    RmDirResp rmdir_resp;
    auto s = stub_->RmDir(&ctx2, rmdir_req, &rmdir_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(rmdir_resp.status(), 0);
}

TEST_F(MetaServiceIntegrationTest, RmDir_NonEmpty_Fails)
{
    grpc::ClientContext ctx1;
    MkDirReq mkdir_req;
    mkdir_req.set_path("/nonempty_dir");
    mkdir_req.set_mode(0755);
    MkDirResp mkdir_resp;
    stub_->MkDir(&ctx1, mkdir_req, &mkdir_resp);

    grpc::ClientContext ctx2;
    CreateFsFileReq file_req;
    file_req.set_path("/nonempty_dir/file.txt");
    file_req.set_mode(0644);
    CreateFsFileResp file_resp;
    stub_->CreateFsFile(&ctx2, file_req, &file_resp);

    grpc::ClientContext ctx3;
    RmDirReq rmdir_req;
    rmdir_req.set_path("/nonempty_dir");
    RmDirResp rmdir_resp;
    auto s = stub_->RmDir(&ctx3, rmdir_req, &rmdir_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_NE(rmdir_resp.status(), 0);
}

// ============================================================
// Rename
// ============================================================
TEST_F(MetaServiceIntegrationTest, Rename_Success)
{
    grpc::ClientContext ctx1;
    CreateFsFileReq req;
    req.set_path("/old.txt");
    req.set_mode(0644);
    CreateFsFileResp resp;
    stub_->CreateFsFile(&ctx1, req, &resp);

    grpc::ClientContext ctx2;
    RenameReq rename_req;
    rename_req.set_src_path("/old.txt");
    rename_req.set_dst_path("/new.txt");
    RenameResp rename_resp;
    auto s = stub_->Rename(&ctx2, rename_req, &rename_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(rename_resp.status(), 0);
}