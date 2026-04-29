#pragma once

#include "common/types.h"
#include "common/config.h"
#include "client/meta_connection.h"
#include "client/data_connection.h"
#include "client/block_splitter.h"
#include <memory>
#include <string>
#include <vector>

namespace openfs
{

    // OpenFS Client SDK - the primary interface for applications.
    // Implements the full write/read pipeline:
    //   Write: CreateFile → Split → AllocateBlocks → WriteBlock → CommitBlocks
    //   Read:  GetFileInfo → GetBlockLocations → ReadBlock → Verify CRC
    class OpenFSClient
    {
    public:
        OpenFSClient() = default;
        ~OpenFSClient() = default;

        // Initialize client with configuration
        Status Init(const ClientConfig &config);

        // ---- File operations ----

        // Create a new file. Returns inode info.
        Status CreateFile(const std::string &path, uint32_t mode,
                          uint64_t file_size, Inode &out_inode);

        // Write data to a file (full file write, replaces existing content).
        // Implements the full write pipeline:
        //   1. Create file in MetaNode
        //   2. Split data into blocks
        //   3. Allocate block locations
        //   4. Write blocks to DataNodes
        //   5. Commit blocks to MetaNode
        Status WriteFile(const std::string &path, uint32_t mode,
                         const void *data, uint64_t data_size);

        // Read entire file data.
        // Implements the read pipeline:
        //   1. Get file info from MetaNode
        //   2. Get block locations
        //   3. Read blocks from DataNodes
        //   4. Verify CRC and assemble data
        Status ReadFile(const std::string &path,
                        std::vector<char> &out_data);

        // Delete a file
        Status DeleteFile(const std::string &path);

        // Get file metadata
        Status GetFileInfo(const std::string &path, Inode &out_inode);

        // Rename a file
        Status Rename(const std::string &src, const std::string &dst);

        // ---- Directory operations ----
        Status MkDir(const std::string &path, uint32_t mode, Inode &out_inode);
        Status ReadDir(const std::string &path, std::vector<DirEntry> &entries);
        Status RmDir(const std::string &path);

    private:
        ClientConfig config_;
        std::unique_ptr<MetaConnection> meta_conn_;
        DataConnection data_conn_;
        BlockSplitter splitter_;
        bool initialized_ = false;

        // Node address cache: node_id -> address
        std::unordered_map<uint64_t, std::string> node_addr_cache_;
    };

} // namespace openfs