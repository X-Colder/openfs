#include "client/openfs_client.h"
#include "common/logging.h"
#include "common/crc32.h"

namespace openfs
{

    Status OpenFSClient::Init(const ClientConfig &config)
    {
        config_ = config;
        meta_conn_ = std::make_unique<MetaConnection>(config.meta_addr);
        initialized_ = true;
        LOG_INFO("OpenFSClient initialized, meta_addr={}", config.meta_addr);
        return Status::kOk;
    }

    Status OpenFSClient::CreateFile(const std::string &path, uint32_t mode,
                                    uint64_t file_size, Inode &out_inode)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->CreateFile(path, mode, 0, 0, file_size, out_inode);
    }

    Status OpenFSClient::WriteFile(const std::string &path, uint32_t mode,
                                   const void *data, uint64_t data_size)
    {
        if (!initialized_)
            return Status::kInternal;

        // Step 1: Create file in MetaNode
        Inode inode;
        Status s = meta_conn_->CreateFile(path, mode, 0, 0, data_size, inode);
        if (s != Status::kOk)
        {
            LOG_ERROR("WriteFile: CreateFile failed for {}, status={}", path, StatusToString(s));
            return s;
        }

        if (data_size == 0)
            return Status::kOk; // Empty file

        // Step 2: Split data into blocks
        auto slices = splitter_.Split(data_size, data);
        if (slices.empty())
            return Status::kInternal;

        BlkLevel level = slices[0].level;
        uint32_t block_count = static_cast<uint32_t>(slices.size());

        // Step 3: Allocate blocks from MetaNode
        std::vector<BlockMeta> allocated;
        s = meta_conn_->AllocateBlocks(inode.inode_id, block_count, level, allocated);
        if (s != Status::kOk)
        {
            LOG_ERROR("WriteFile: AllocateBlocks failed for {}, status={}", path, StatusToString(s));
            return s;
        }

        if (allocated.size() != slices.size())
        {
            LOG_ERROR("WriteFile: allocated {} blocks but need {}", allocated.size(), slices.size());
            return Status::kInternal;
        }

        // Step 4: Write each block to DataNode
        std::vector<BlockMeta> committed_blocks;
        committed_blocks.reserve(allocated.size());

        for (size_t i = 0; i < allocated.size(); ++i)
        {
            const auto &alloc = allocated[i];
            const auto &slice = slices[i];

            // Resolve DataNode address from node_id
            std::string data_addr;
            auto it = node_addr_cache_.find(alloc.node_id);
            if (it != node_addr_cache_.end())
            {
                data_addr = it->second;
            }
            else
            {
                // In single-node mode, use the meta_addr pattern to derive data addr
                // For now, assume DataNode is on the same host with port 8200
                // TODO: query node address from MetaNode
                data_addr = "localhost:8200";
                node_addr_cache_[alloc.node_id] = data_addr;
            }

            const uint8_t *block_data = static_cast<const uint8_t *>(data) + slice.file_offset;
            uint64_t out_seg_id = 0;
            uint64_t out_offset = 0;

            s = data_conn_.WriteBlock(data_addr, alloc.block_id, alloc.segment_id,
                                      block_data, slice.data_size, slice.crc32,
                                      out_seg_id, out_offset);
            if (s != Status::kOk)
            {
                LOG_ERROR("WriteFile: WriteBlock {} failed, status={}", i, StatusToString(s));
                return s;
            }

            // Build committed block meta with actual write location
            BlockMeta committed = alloc;
            committed.segment_id = out_seg_id;
            committed.offset = out_offset;
            committed.size = slice.data_size;
            committed.crc32 = slice.crc32;
            committed_blocks.push_back(committed);
        }

        // Step 5: Commit blocks to MetaNode
        s = meta_conn_->CommitBlocks(inode.inode_id, committed_blocks);
        if (s != Status::kOk)
        {
            LOG_ERROR("WriteFile: CommitBlocks failed for {}, status={}", path, StatusToString(s));
            return s;
        }

        LOG_INFO("WriteFile: {} written successfully, {} blocks, {} bytes",
                 path, block_count, data_size);
        return Status::kOk;
    }

    Status OpenFSClient::ReadFile(const std::string &path,
                                  std::vector<char> &out_data)
    {
        if (!initialized_)
            return Status::kInternal;

        // Step 1: Get file info
        Inode inode;
        Status s = meta_conn_->GetFileInfo(path, inode);
        if (s != Status::kOk)
        {
            LOG_ERROR("ReadFile: GetFileInfo failed for {}, status={}", path, StatusToString(s));
            return s;
        }

        if (inode.size == 0)
        {
            out_data.clear();
            return Status::kOk;
        }

        // Step 2: Get block locations
        std::vector<BlockMeta> blocks;
        s = meta_conn_->GetBlockLocations(inode.inode_id, blocks);
        if (s != Status::kOk)
        {
            LOG_ERROR("ReadFile: GetBlockLocations failed for {}, status={}", path, StatusToString(s));
            return s;
        }

        if (blocks.empty())
        {
            LOG_ERROR("ReadFile: no blocks found for inode {}", inode.inode_id);
            return Status::kNotFound;
        }

        // Step 3: Read blocks from DataNodes and assemble
        out_data.resize(inode.size);
        uint64_t write_offset = 0;

        for (const auto &block : blocks)
        {
            // Resolve DataNode address
            std::string data_addr;
            auto it = node_addr_cache_.find(block.node_id);
            if (it != node_addr_cache_.end())
            {
                data_addr = it->second;
            }
            else
            {
                data_addr = "localhost:8200";
                node_addr_cache_[block.node_id] = data_addr;
            }

            std::vector<char> block_data;
            uint32_t block_crc = 0;

            s = data_conn_.ReadBlock(data_addr, block.segment_id, block.offset,
                                     block_data, block_crc);
            if (s != Status::kOk)
            {
                LOG_ERROR("ReadFile: ReadBlock {} failed, status={}", block.block_id, StatusToString(s));
                return s;
            }

            // Step 4: Verify CRC
            uint32_t computed_crc = ComputeCRC32(block_data.data(),
                                                 static_cast<uint32_t>(block_data.size()));
            if (computed_crc != block_crc)
            {
                LOG_ERROR("ReadFile: CRC mismatch for block {}, expected={}, got={}",
                          block.block_id, block_crc, computed_crc);
                return Status::kCRCMismatch;
            }

            // Assemble data
            uint32_t copy_size = static_cast<uint32_t>(std::min<uint64_t>(
                block_data.size(), inode.size - write_offset));
            if (write_offset + copy_size <= out_data.size())
            {
                std::memcpy(out_data.data() + write_offset, block_data.data(), copy_size);
            }
            write_offset += copy_size;
        }

        LOG_INFO("ReadFile: {} read successfully, {} bytes", path, inode.size);
        return Status::kOk;
    }

    Status OpenFSClient::DeleteFile(const std::string &path)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->DeleteFile(path);
    }

    Status OpenFSClient::GetFileInfo(const std::string &path, Inode &out_inode)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->GetFileInfo(path, out_inode);
    }

    Status OpenFSClient::Rename(const std::string &src, const std::string &dst)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->Rename(src, dst);
    }

    Status OpenFSClient::MkDir(const std::string &path, uint32_t mode, Inode &out_inode)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->MkDir(path, mode, 0, 0, out_inode);
    }

    Status OpenFSClient::ReadDir(const std::string &path, std::vector<DirEntry> &entries)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->ReadDir(path, entries);
    }

    Status OpenFSClient::RmDir(const std::string &path)
    {
        if (!initialized_)
            return Status::kInternal;
        return meta_conn_->RmDir(path);
    }

} // namespace openfs