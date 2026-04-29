#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/create_channel.h>
#include <memory>
#include <filesystem>
#include <vector>
#include "data/data_node.h"
#include "data/data_service_impl.h"
#include "common/crc32.h"
#include "data_service.grpc.pb.h"

using namespace openfs;

class DataServiceIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<grpc::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<DataService::Stub> stub_;
    std::unique_ptr<DataServiceImpl> service_;
    DataNodeConfig data_config_;
    int chosen_port_ = 0;

    void SetUp() override
    {
        // Create temp data directory
        test_dir_ = std::filesystem::temp_directory_path().string() + "/openfs_test_data_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(test_dir_);

        data_config_.listen_addr = "localhost:0";
        data_config_.data_dir = test_dir_;
        data_config_.meta_addr = "localhost:8100"; // won't connect in this test
    }

    void TearDown() override
    {
        if (server_)
            server_->Shutdown();
        // Release data_node_ and service_ first to close file handles
        service_.reset();
        data_node_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    void StartServerWithNode()
    {
        auto data_node = std::make_unique<DataNode>(data_config_);
        service_ = std::make_unique<DataServiceImpl>(*data_node);
        data_node_ = std::move(data_node);

        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &chosen_port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);

        std::string addr = "localhost:" + std::to_string(chosen_port_);
        channel_ = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        stub_ = DataService::NewStub(channel_);
    }

    std::string test_dir_;
    std::unique_ptr<DataNode> data_node_;
};

// ============================================================
// WriteBlock + ReadBlock
// ============================================================
TEST_F(DataServiceIntegrationTest, WriteAndReadBlock)
{
    StartServerWithNode();

    // Prepare data
    std::vector<char> write_data(4096, 'X');
    uint32_t crc = ComputeCRC32(write_data.data(), 4096);

    // Write
    grpc::ClientContext ctx1;
    WriteBlockReq write_req;
    write_req.set_block_id(1);
    write_req.set_data(std::string(write_data.begin(), write_data.end()));
    write_req.set_crc32(crc);

    WriteBlockResp write_resp;
    auto s = stub_->WriteBlock(&ctx1, write_req, &write_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(write_resp.status(), 0);
    EXPECT_GT(write_resp.segment_id(), 0u);
    EXPECT_GT(write_resp.offset(), 0u);

    // Read back
    grpc::ClientContext ctx2;
    ReadBlockReq read_req;
    read_req.set_segment_id(write_resp.segment_id());
    read_req.set_offset(write_resp.offset());

    ReadBlockResp read_resp;
    s = stub_->ReadBlock(&ctx2, read_req, &read_resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(read_resp.status(), 0);
    EXPECT_EQ(read_resp.data().size(), 4096u);
    EXPECT_EQ(read_resp.crc32(), crc);
}

TEST_F(DataServiceIntegrationTest, WriteBlock_LargeData)
{
    StartServerWithNode();

    size_t data_size = 2 * 1024 * 1024; // 2MB
    std::vector<char> write_data(data_size);
    for (size_t i = 0; i < data_size; ++i)
        write_data[i] = static_cast<char>(i % 256);
    uint32_t crc = ComputeCRC32(write_data.data(), static_cast<uint32_t>(data_size));

    grpc::ClientContext ctx1;
    WriteBlockReq req;
    req.set_block_id(10);
    req.set_data(std::string(write_data.begin(), write_data.end()));
    req.set_crc32(crc);

    WriteBlockResp resp;
    auto s = stub_->WriteBlock(&ctx1, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.status(), 0);

    // Read back and verify data
    grpc::ClientContext ctx2;
    ReadBlockReq read_req;
    read_req.set_segment_id(resp.segment_id());
    read_req.set_offset(resp.offset());

    ReadBlockResp read_resp;
    stub_->ReadBlock(&ctx2, read_req, &read_resp);
    EXPECT_EQ(read_resp.data().size(), data_size);
    EXPECT_EQ(read_resp.crc32(), crc);
}

// ============================================================
// ReadBlock error cases
// ============================================================
TEST_F(DataServiceIntegrationTest, ReadBlock_NonexistentSegment)
{
    StartServerWithNode();

    grpc::ClientContext ctx;
    ReadBlockReq req;
    req.set_segment_id(9999);
    req.set_offset(4096);

    ReadBlockResp resp;
    auto s = stub_->ReadBlock(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_NE(resp.status(), 0);
}

// ============================================================
// Multiple writes, independent reads
// ============================================================
TEST_F(DataServiceIntegrationTest, MultipleWrites_IndependentReads)
{
    StartServerWithNode();

    struct WriteInfo
    {
        uint64_t segment_id;
        uint64_t offset;
        uint32_t crc;
        std::vector<char> data;
    };
    std::vector<WriteInfo> writes;

    for (int i = 0; i < 5; ++i)
    {
        size_t sz = 1024 * (i + 1);
        std::vector<char> data(sz, static_cast<char>('A' + i));
        uint32_t crc = ComputeCRC32(data.data(), static_cast<uint32_t>(sz));

        grpc::ClientContext ctx;
        WriteBlockReq req;
        req.set_block_id(100 + i);
        req.set_data(std::string(data.begin(), data.end()));
        req.set_crc32(crc);

        WriteBlockResp resp;
        stub_->WriteBlock(&ctx, req, &resp);
        ASSERT_EQ(resp.status(), 0);

        writes.push_back({resp.segment_id(), resp.offset(), crc, std::move(data)});
    }

    // Read each back
    for (int i = 0; i < 5; ++i)
    {
        grpc::ClientContext ctx;
        ReadBlockReq req;
        req.set_segment_id(writes[i].segment_id);
        req.set_offset(writes[i].offset);

        ReadBlockResp resp;
        auto s = stub_->ReadBlock(&ctx, req, &resp);
        EXPECT_TRUE(s.ok());
        EXPECT_EQ(resp.crc32(), writes[i].crc);
        EXPECT_EQ(static_cast<size_t>(resp.data().size()), writes[i].data.size());
    }
}