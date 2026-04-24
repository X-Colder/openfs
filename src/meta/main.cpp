#include "meta/meta_node.h"
#include "common/logging.h"
#include "common/config.h"
#include <iostream>

using namespace openfs;

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    InitLogging("meta_node");

    auto &config = Config::Instance();
    if (!config.LoadFromFile(argv[1]))
    {
        LOG_ERROR("Failed to load configuration from {}", argv[1]);
        return 1;
    }

    const auto &meta_config = config.GetMetaConfig();
    LOG_INFO("Starting MetaNode on {}", meta_config.listen_addr);

    MetaNode meta_node(meta_config);
    if (meta_node.Start() != Status::kOk)
    {
        LOG_ERROR("Failed to start MetaNode");
        return 1;
    }

    meta_node.Wait();
    meta_node.Stop();
    LOG_INFO("MetaNode stopped");
    return 0;
}