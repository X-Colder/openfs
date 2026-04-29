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
        LOG_DEBUG("WriteBlock: block_id={}, data_size={}",
                  request->block_id(), request->data().size());

        // Verify CRC32
        uint32_t calc_crc = ComputeCRC32(request->data().data(), request->data().size());
        if (calc_crc != request->crc32())
        {
            LOG_ERROR("CRC32 mismatch for block {}: expected={}, calculated={}",
                      request->block_id(), request->crc32(), calc_crc);
            response->set_status(static_cast<int32_t>(Status::kCRCMismatch));
            return grpc::Status::OK;
        }

        // Select block level based on data size
        BlkLevel level = SelectBlockLevel(request->data().size());

        uint32_t disk_id = 0;
        uint64_t offset = 0;
        Status s = data_node_.WriteBlock(
            request->block_id(), level,
            request->data().data(), request->data().size(),
            request->crc32(), disk_id, offset);

        response->set_status(static_cast<int32_t>(s));
        response->set_segment_id(disk_id); // Reuse segment_id field for disk_id
        response->set_offset(offset);
        return grpc::Status::OK;
    }

    grpc::Status DataServiceImpl::ReadBlock(grpc::ServerContext *context,
                                            const ReadBlockReq *request,
                                            ReadBlockResp *response)
    {
        LOG_DEBUG("ReadBlock: segment_id={}, offset={}",
                  request->segment_id(), request->offset());

        std::vector<char> data;
        uint32_t crc32 = 0;
        uint64_t block_id = 0;

        // segment_id is reused as disk_id
        Status s = data_node_.ReadBlock(
            static_cast<uint32_t>(request->segment_id()),
            request->offset(), data, crc32, block_id);

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

        Status s = data_node_.DeleteBlock(request->block_id());
        response->set_status(static_cast<int32_t>(s));
        return grpc::Status::OK;
    }

} // namespace openfs