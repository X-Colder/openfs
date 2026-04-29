#pragma once

#include "common/types.h"
#include "common/config.h"
#include <grpcpp/grpcpp.h>
#include "meta_service.grpc.pb.h"
#include "node_service.grpc.pb.h"
#include <memory>
#include <string>
#include <mutex>

namespace openfs
{

    // Connection manager to MetaCluster.
    // Handles channel creation, stub caching, and automatic leader routing.
    class MetaConnection
    {
    public:
        explicit MetaConnection(const std::string &meta_addr);
        ~MetaConnection() = default;

        // --- File operations ---
        Status CreateFile(const std::string &path, uint32_t mode,
                          uint32_t uid, uint32_t gid, uint64_t file_size,
                          Inode &out_inode);

        Status DeleteFile(const std::string &path);

        Status GetFileInfo(const std::string &path, Inode &out_inode);

        Status Rename(const std::string &src, const std::string &dst);

        // --- Directory operations ---
        Status MkDir(const std::string &path, uint32_t mode,
                     uint32_t uid, uint32_t gid, Inode &out_inode);

        Status ReadDir(const std::string &path, std::vector<DirEntry> &entries);

        Status RmDir(const std::string &path);

        // --- Block allocation ---
        Status AllocateBlocks(uint64_t inode_id, uint32_t block_count,
                              BlkLevel level, std::vector<BlockMeta> &blocks);

        Status CommitBlocks(uint64_t inode_id,
                            const std::vector<BlockMeta> &blocks);

        Status GetBlockLocations(uint64_t inode_id,
                                 std::vector<BlockMeta> &blocks);

    private:
        std::shared_ptr<grpc::Channel> channel_;
        std::unique_ptr<MetaService::Stub> stub_;
        std::string meta_addr_;
        std::mutex mutex_;

        // Helper: convert proto InodeProto to Inode
        static Inode ProtoToInode(const InodeProto &proto);
        // Helper: convert Inode to proto
        static void InodeToProto(const Inode &inode, InodeProto &proto);
        // Helper: convert BlockMetaProto to BlockMeta
        static BlockMeta ProtoToBlockMeta(const BlockMetaProto &proto);
        // Helper: convert BlockMeta to proto
        static void BlockMetaToProto(const BlockMeta &meta, BlockMetaProto &proto);
    };

} // namespace openfs