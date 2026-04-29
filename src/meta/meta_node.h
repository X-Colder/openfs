#pragma once

#include "common/types.h"
#include "common/config.h"
#include "meta/meta_service_impl.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <grpcpp/grpcpp.h>

namespace openfs
{

    class MetaNode
    {
    public:
        explicit MetaNode(const MetaNodeConfig &config);

        Status Start();
        Status Stop();
        void Wait();

    private:
        void HealthCheckLoop();

        MetaNodeConfig config_;
        MetaServiceImpl meta_service_;
        std::unique_ptr<NodeServiceImpl> node_service_;

        std::unique_ptr<grpc::Server> server_;
        std::atomic<bool> running_{false};
        std::thread health_thread_;
    };

} // namespace openfs