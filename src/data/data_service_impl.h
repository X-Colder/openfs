#pragma once

#include "common/types.h"
#include "common/config.h"
#include "data/data_node.h"
#include <grpcpp/grpcpp.h>
#include "data_service.grpc.pb.h"

namespace openfs
{

    class DataServiceImpl final : public DataService::Service
    {
    public:
        explicit DataServiceImpl(DataNode &data_node);

        grpc::Status WriteBlock(grpc::ServerContext *context,
                                const WriteBlockReq *request,
                                WriteBlockResp *response) override;

        grpc::Status ReadBlock(grpc::ServerContext *context,
                               const ReadBlockReq *request,
                               ReadBlockResp *response) override;

        grpc::Status DeleteBlock(grpc::ServerContext *context,
                                 const DeleteBlockReq *request,
                                 DeleteBlockResp *response) override;

    private:
        DataNode &data_node_;
    };

} // namespace openfs