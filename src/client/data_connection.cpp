#include "client/data_connection.h"
#include "common/logging.h"

namespace openfs
{

    DataService::Stub *DataConnection::GetOrCreateStub(const std::string &addr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stubs_.find(addr);
        if (it != stubs_.end())
            return it->second.get();

        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        auto stub = DataService::NewStub(channel);
        auto *ptr = stub.get();
        channels_[addr] = channel;
        stubs_[addr] = std::move(stub);
        return ptr;
    }

    Status DataConnection::WriteBlock(const std::string &data_node_addr,
                                      uint64_t block_id, uint64_t segment_id,
                                      const void *data, uint32_t data_size,
                                      uint32_t crc32,
                                      uint64_t &out_segment_id, uint64_t &out_offset)
    {
        auto *stub = GetOrCreateStub(data_node_addr);
        if (!stub)
            return Status::kInternal;

        grpc::ClientContext ctx;
        WriteBlockReq req;
        req.set_block_id(block_id);
        req.set_segment_id(segment_id);
        req.set_crc32(crc32);
        req.set_data(static_cast<const char *>(data), data_size);
        WriteBlockResp resp;

        auto s = stub->WriteBlock(&ctx, req, &resp);
        if (!s.ok())
        {
            LOG_ERROR("WriteBlock RPC to {} failed: {}", data_node_addr, s.error_message());
            return Status::kInternal;
        }
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        out_segment_id = resp.segment_id();
        out_offset = resp.offset();
        return Status::kOk;
    }

    Status DataConnection::ReadBlock(const std::string &data_node_addr,
                                     uint64_t segment_id, uint64_t offset,
                                     std::vector<char> &out_data, uint32_t &out_crc32)
    {
        auto *stub = GetOrCreateStub(data_node_addr);
        if (!stub)
            return Status::kInternal;

        grpc::ClientContext ctx;
        ReadBlockReq req;
        req.set_segment_id(segment_id);
        req.set_offset(offset);
        ReadBlockResp resp;

        auto s = stub->ReadBlock(&ctx, req, &resp);
        if (!s.ok())
        {
            LOG_ERROR("ReadBlock RPC to {} failed: {}", data_node_addr, s.error_message());
            return Status::kInternal;
        }
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        out_crc32 = resp.crc32();
        const std::string &d = resp.data();
        out_data.assign(d.begin(), d.end());
        return Status::kOk;
    }

    Status DataConnection::DeleteBlock(const std::string &data_node_addr,
                                       uint64_t block_id)
    {
        auto *stub = GetOrCreateStub(data_node_addr);
        if (!stub)
            return Status::kInternal;

        grpc::ClientContext ctx;
        DeleteBlockReq req;
        req.set_block_id(block_id);
        DeleteBlockResp resp;

        auto s = stub->DeleteBlock(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        return static_cast<Status>(resp.status());
    }

} // namespace openfs