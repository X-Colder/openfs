#pragma once

#include "common/types.h"
#include "common/config.h"
#include "meta/inode_table.h"
#include "meta/namespace_manager.h"
#include "meta/block_map.h"
#include "meta/block_allocator.h"
#include "meta/node_manager.h"
#include <grpcpp/grpcpp.h>
#include "meta_service.grpc.pb.h"
#include "node_service.grpc.pb.h"

namespace openfs
{

    // NodeServiceImpl: handles DataNode registration, heartbeat, block reporting
    class NodeServiceImpl final : public NodeService::Service
    {
    public:
        explicit NodeServiceImpl(NodeManager &node_mgr, BlockMap &block_map);

        grpc::Status Register(grpc::ServerContext *ctx, const RegisterReq *req,
                              RegisterResp *resp) override;
        grpc::Status Heartbeat(grpc::ServerContext *ctx, const HeartbeatReq *req,
                               HeartbeatResp *resp) override;
        grpc::Status ReportBlocks(grpc::ServerContext *ctx, const ReportBlocksReq *req,
                                  ReportBlocksResp *resp) override;

    private:
        NodeManager &node_mgr_;
        BlockMap &block_map_;
    };

    class MetaServiceImpl final : public MetaService::Service
    {
    public:
        MetaServiceImpl();

        // File operations
        grpc::Status CreateFsFile(grpc::ServerContext *ctx, const CreateFsFileReq *req,
                                  CreateFsFileResp *resp) override;
        grpc::Status RemoveFsFile(grpc::ServerContext *ctx, const RemoveFsFileReq *req,
                                  RemoveFsFileResp *resp) override;
        grpc::Status GetFileInfo(grpc::ServerContext *ctx, const GetFileInfoReq *req,
                                 GetFileInfoResp *resp) override;
        grpc::Status Rename(grpc::ServerContext *ctx, const RenameReq *req,
                            RenameResp *resp) override;

        // Directory operations
        grpc::Status MkDir(grpc::ServerContext *ctx, const MkDirReq *req,
                           MkDirResp *resp) override;
        grpc::Status ReadDir(grpc::ServerContext *ctx, const ReadDirReq *req,
                             ReadDirResp *resp) override;
        grpc::Status RmDir(grpc::ServerContext *ctx, const RmDirReq *req,
                           RmDirResp *resp) override;

        // Block allocation & commit
        grpc::Status AllocateBlocks(grpc::ServerContext *ctx, const AllocBlocksReq *req,
                                    AllocBlocksResp *resp) override;
        grpc::Status CommitBlocks(grpc::ServerContext *ctx, const CommitBlocksReq *req,
                                  CommitBlocksResp *resp) override;
        grpc::Status GetBlockLocations(grpc::ServerContext *ctx, const GetBlockLocsReq *req,
                                       GetBlockLocsResp *resp) override;

        // Internal accessors for MetaNode
        InodeTable &GetInodeTable() { return inode_table_; }
        NamespaceManager &GetNamespaceManager() { return namespace_mgr_; }
        BlockMap &GetBlockMap() { return block_map_; }
        BlockAllocator &GetBlockAllocator() { return block_allocator_; }
        NodeManager &GetNodeManager() { return node_mgr_; }

    private:
        InodeTable inode_table_;
        NamespaceManager namespace_mgr_;
        BlockMap block_map_;
        NodeManager node_mgr_;
        BlockAllocator block_allocator_;

        void FillInodeProto(const Inode &inode, InodeProto *proto);
        void FillDentryProto(const DirEntry &entry, DentryProto *proto);
        void FillBlockMetaProto(const BlockMeta &block, BlockMetaProto *proto);
    };

} // namespace openfs