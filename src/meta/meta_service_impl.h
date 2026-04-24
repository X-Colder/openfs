#pragma once

#include "common/types.h"
#include "common/config.h"
#include "meta/inode_table.h"
#include "meta/namespace_manager.h"
#include <grpcpp/grpcpp.h>
#include "meta_service.grpc.pb.h"

namespace openfs
{

    class MetaServiceImpl final : public MetaService::Service
    {
    public:
        MetaServiceImpl();

        // File operations
        grpc::Status CreateFile(grpc::ServerContext *ctx, const CreateFileReq *req,
                                CreateFileResp *resp) override;
        grpc::Status DeleteFile(grpc::ServerContext *ctx, const DeleteFileReq *req,
                                DeleteFileResp *resp) override;
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

    private:
        InodeTable inode_table_;
        NamespaceManager namespace_mgr_;

        void FillInodeProto(const Inode &inode, InodeProto *proto);
        void FillDentryProto(const DirEntry &entry, DentryProto *proto);
    };

} // namespace openfs