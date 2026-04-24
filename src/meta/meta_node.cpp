#include "meta/meta_node.h"
#include "common/logging.h"

namespace openfs
{

    MetaNode::MetaNode(const MetaNodeConfig &config)
        : config_(config) {}

    Status MetaNode::Start()
    {
        LOG_INFO("Starting MetaNode on {}", config_.listen_addr);

        grpc::ServerBuilder builder;
        builder.AddListeningPort(config_.listen_addr, grpc::InsecureServerCredentials());
        builder.RegisterService(&meta_service_);

        server_ = builder.BuildAndStart();
        if (!server_)
        {
            LOG_ERROR("Failed to start MetaNode gRPC server on {}", config_.listen_addr);
            return Status::kInternal;
        }

        running_ = true;
        health_thread_ = std::thread([this]()
                                     { this->HealthCheckLoop(); });

        LOG_INFO("MetaNode started successfully on {}", config_.listen_addr);
        return Status::kOk;
    }

    Status MetaNode::Stop()
    {
        LOG_INFO("Stopping MetaNode...");
        running_ = false;

        if (server_)
        {
            server_->Shutdown();
        }

        if (health_thread_.joinable())
        {
            health_thread_.join();
        }

        LOG_INFO("MetaNode stopped");
        return Status::kOk;
    }

    void MetaNode::Wait()
    {
        if (server_)
        {
            server_->Wait();
        }
    }

    void MetaNode::HealthCheckLoop()
    {
        while (running_)
        {
            // TODO: Check DataNode heartbeats, detect failures
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

} // namespace openfs