#include "common/config.h"
#include "common/logging.h"
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace openfs
{

    Config &Config::Instance()
    {
        static Config instance;
        return instance;
    }

    bool Config::LoadFromFile(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open config file: {}", path);
            return false;
        }

        std::unordered_map<std::string, std::string> kv;
        std::string line;
        while (std::getline(file, line))
        {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#')
                continue;
            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            // Trim whitespace
            auto trim = [](std::string &s)
            {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
            };
            trim(key);
            trim(value);
            kv[key] = value;
        }

        // Populate MetaNodeConfig
        if (kv.count("meta.listen_addr"))
            meta_config_.listen_addr = kv["meta.listen_addr"];
        if (kv.count("meta.data_dir"))
            meta_config_.data_dir = kv["meta.data_dir"];
        if (kv.count("meta.node_id"))
            meta_config_.node_id = static_cast<uint16_t>(std::stoul(kv["meta.node_id"]));
        if (kv.count("meta.raft_peers"))
            meta_config_.raft_peers = kv["meta.raft_peers"];

        // Populate DataNodeConfig
        if (kv.count("data.listen_addr"))
            data_config_.listen_addr = kv["data.listen_addr"];
        if (kv.count("data.data_dir"))
            data_config_.data_dir = kv["data.data_dir"];
        if (kv.count("data.meta_addr"))
            data_config_.meta_addr = kv["data.meta_addr"];
        if (kv.count("data.segment_size"))
            data_config_.segment_size = std::stoull(kv["data.segment_size"]);
        if (kv.count("data.max_segments"))
            data_config_.max_segments = static_cast<uint32_t>(std::stoul(kv["data.max_segments"]));

        // Populate ClientConfig
        if (kv.count("client.meta_addr"))
            client_config_.meta_addr = kv["client.meta_addr"];

        LOG_INFO("Loaded config from {}", path);
        return true;
    }

} // namespace openfs