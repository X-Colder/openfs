#include "data/data_node.h"
#include "data/data_service_impl.h"
#include "common/logging.h"
#include "common/config.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <csignal>

using namespace openfs;

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    InitLogging("data_node");

    auto &config = Config::Instance();
    if (!config.LoadFromFile(argv[1]))
    {
        LOG_ERROR("Failed to load configuration from {}", argv[1]);
        return 1;
    }

    const auto &data_config = config.GetDataConfig();
    LOG_INFO("Starting DataNode on {}", data_config.listen_addr);

    DataNode data_node(data_config);
    if (data_node.Start() != Status::kOk)
    {
        LOG_ERROR("Failed to start DataNode");
        return 1;
    }

    DataServiceImpl service(data_node);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(data_config.listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server)
    {
        LOG_ERROR("Failed to start gRPC server");
        return 1;
    }

    LOG_INFO("DataNode gRPC server listening on {}", data_config.listen_addr);
    server->Wait();

    data_node.Stop();
    LOG_INFO("DataNode stopped");
    return 0;
}