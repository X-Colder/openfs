#include "meta/meta_node_client.h"
#include "common/logging.h"

namespace openfs
{

    MetaNodeClient::MetaNodeClient(const std::string &meta_addr)
    {
        channel_ = grpc::CreateChannel(meta_addr, grpc::InsecureChannelCredentials());
        stub_ = NodeService::NewStub(channel_);
        LOG_INFO("MetaNodeClient connecting to {}", meta_addr);
    }

    Status MetaNodeClient::Register(const std::string &address, uint64_t capacity,
                                    uint64_t &assigned_node_id)
    {
        RegisterReq req;
        req.set_address(address);
        req.set_capacity(capacity);

        RegisterResp resp;
        grpc::ClientContext ctx;

        grpc::Status grpc_status = stub_->Register(&ctx, req, &resp);
        if (!grpc_status.ok())
        {
            LOG_ERROR("Register RPC failed: {}", grpc_status.error_message());
            return Status::kInternal;
        }

        if (resp.status() != static_cast<int32_t>(Status::kOk))
        {
            LOG_ERROR("Register returned error status: {}", resp.status());
            return static_cast<Status>(resp.status());
        }

        assigned_node_id = resp.node_id();
        LOG_INFO("Registered with MetaNode, assigned node_id={}", assigned_node_id);
        return Status::kOk;
    }

    Status MetaNodeClient::Heartbeat(uint64_t node_id, uint64_t used,
                                     uint32_t block_count, float cpu_load)
    {
        HeartbeatReq req;
        req.set_node_id(node_id);
        req.set_timestamp(NowNs());
        req.set_block_count(block_count);
        req.set_cpu_load(cpu_load);

        // Add disk usage info
        auto *disk = req.add_disk_usage();
        disk->set_used(used);

        HeartbeatResp resp;
        grpc::ClientContext ctx;

        grpc::Status grpc_status = stub_->Heartbeat(&ctx, req, &resp);
        if (!grpc_status.ok())
        {
            LOG_WARN("Heartbeat RPC failed: {}", grpc_status.error_message());
            return Status::kInternal;
        }

        // Handle commands from meta (e.g., blocks to delete)
        if (resp.blocks_to_delete_size() > 0)
        {
            LOG_INFO("MetaNode requests deletion of {} blocks", resp.blocks_to_delete_size());
            // TODO: process blocks_to_delete
        }

        return Status::kOk;
    }

    Status MetaNodeClient::ReportBlocks(uint64_t node_id, const std::vector<uint64_t> &block_ids)
    {
        ReportBlocksReq req;
        req.set_node_id(node_id);
        for (uint64_t bid : block_ids)
        {
            req.add_block_ids(bid);
        }

        ReportBlocksResp resp;
        grpc::ClientContext ctx;

        grpc::Status grpc_status = stub_->ReportBlocks(&ctx, req, &resp);
        if (!grpc_status.ok())
        {
            LOG_WARN("ReportBlocks RPC failed: {}", grpc_status.error_message());
            return Status::kInternal;
        }

        return Status::kOk;
    }

} // namespace openfs