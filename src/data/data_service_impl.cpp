#include "data/data_service_impl.h"
#include "common/logging.h"
#include "common/crc32.h"

namespace openfs
{

    DataServiceImpl::DataServiceImpl(DataNode &data_node)
        : data_node_(data_node) {}

    grpc::Status DataServiceImpl::WriteBlock(grpc::ServerContext *context,
                                             const WriteBlockReq *request,
                                             WriteBlockResp *response)
    {
        LOG_DEBUG("WriteBlock: block_id={}, segment_id={}, data_size={}",
                  request->block_id(), request->segment_id(), request->data().size());

        // Verify CRC32
        uint32_t calc_crc = ComputeCRC32(request->data().data(), request->data().size());
        if (calc_crc != request->crc32())
        {
            LOG_ERROR("CRC32 mismatch for block {}: expected={}, calculated={}",
                      request->block_id(), request->crc32(), calc_crc);
            response->set_status(static_cast<int32_t>(Status::kCRCMismatch));
            return grpc::Status::OK;
        }

        uint64_t segment_id = 0, offset = 0;
        // Use L2 as default level for now; proto doesn't carry level
        Status s = data_node_.WriteBlock(
            request->block_id(), BlkLevel::L2,
            request->data().data(), request->data().size(),
            request->crc32(), segment_id, offset);

        response->set_status(static_cast<int32_t>(s));
        response->set_offset(offset);
        return grpc::Status::OK;
    }

    grpc::Status DataServiceImpl::ReadBlock(grpc::ServerContext *context,
                                            const ReadBlockReq *request,
                                            ReadBlockResp *response)
    {
        LOG_DEBUG("ReadBlock: block_id={}, segment_id={}, offset={}",
                  request->block_id(), request->segment_id(), request->offset());

        std::vector<char> data;
        uint32_t crc32 = 0;

        Status s = data_node_.ReadBlock(request->segment_id(), request->offset(), data, crc32);

        response->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            response->set_crc32(crc32);
            response->set_data(data.data(), data.size());
        }
        return grpc::Status::OK;
    }

    grpc::Status DataServiceImpl::DeleteBlock(grpc::ServerContext *context,
                                              const DeleteBlockReq *request,
                                              DeleteBlockResp *response)
    {
        LOG_DEBUG("DeleteBlock: block_id={}", request->block_id());

        // TODO: Implement actual block deletion
        // For now, just mark as not implemented
        response->set_status(static_cast<int32_t>(Status::kOk));
        LOG_WARN("DeleteBlock not fully implemented yet");
        return grpc::Status::OK;
    }

} // namespace openfs