#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace openfs
{

    struct MetaNodeConfig
    {
        std::string listen_addr = "0.0.0.0:8100";
        std::string data_dir = "/var/lib/openfs/meta";
        uint16_t node_id = 0;
        // Raft peers (comma separated, e.g., "host1:8100,host2:8100")
        std::string raft_peers;
    };

    struct DataNodeConfig
    {
        std::string listen_addr = "0.0.0.0:8200";
        std::string data_dir = "/var/lib/openfs/data";
        std::string meta_addr = "127.0.0.1:8100";
        uint64_t segment_size = 256ULL * 1024 * 1024; // 256MB (legacy)
        uint32_t max_segments = 1024;                  // legacy

        // New: disk-level storage pool configuration
        // Each disk path can be a file (for testing) or a raw device path
        std::vector<std::string> disk_paths;
        // Default disk size in bytes when formatting new file-based disks
        uint64_t disk_size = 16ULL * 1024 * 1024; // 16MB default for testing
        // Number of WAL blocks per disk
        uint64_t wal_blocks = 256; // 256 * 4KB = 1MB
    };

    struct ClientConfig
    {
        std::string meta_addr = "127.0.0.1:8100";
        uint32_t batch_size = 16;                   // blocks per batch
        uint64_t batch_bytes = 64ULL * 1024 * 1024; // 64MB
        uint32_t connect_timeout_ms = 5000;
        uint32_t rpc_timeout_ms = 30000;
        uint64_t read_cache_size = 256ULL * 1024 * 1024; // 256MB L1 cache
    };

    // Load configuration from file
    class Config
    {
    public:
        static Config &Instance();

        bool LoadFromFile(const std::string &path);

        const MetaNodeConfig &GetMetaConfig() const { return meta_config_; }
        const DataNodeConfig &GetDataConfig() const { return data_config_; }
        const ClientConfig &GetClientConfig() const { return client_config_; }

        MetaNodeConfig &MutableMetaConfig() { return meta_config_; }
        DataNodeConfig &MutableDataConfig() { return data_config_; }
        ClientConfig &MutableClientConfig() { return client_config_; }

    private:
        Config() = default;
        MetaNodeConfig meta_config_;
        DataNodeConfig data_config_;
        ClientConfig client_config_;
    };

} // namespace openfs