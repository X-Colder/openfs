#include "meta/meta_service_impl.h"
#include "common/logging.h"
#include "common/id_generator.h"

namespace openfs
{

    MetaServiceImpl::MetaServiceImpl()
        : namespace_mgr_(inode_table_) {}

    // ---- Helper: Inode -> InodeProto ----
    void MetaServiceImpl::FillInodeProto(const Inode &inode, InodeProto *proto)
    {
        proto->set_inode_id(inode.inode_id);
        if (inode.file_type == openfs::InodeType::kDirectory)
        {
            proto->set_file_type(FILE_TYPE_DIRECTORY);
        }
        else if (inode.file_type == openfs::InodeType::kSymlink)
        {
            proto->set_file_type(FILE_TYPE_SYMLINK);
        }
        else
        {
            proto->set_file_type(FILE_TYPE_REGULAR);
        }
        proto->set_mode(inode.mode);
        proto->set_uid(inode.uid);
        proto->set_gid(inode.gid);
        proto->set_size(inode.size);
        proto->set_nlink(inode.nlink);
        proto->set_atime_ns(inode.atime_ns);
        proto->set_mtime_ns(inode.mtime_ns);
        proto->set_ctime_ns(inode.ctime_ns);
        proto->set_block_level(static_cast<::openfs::BlockLevel>(static_cast<int>(inode.block_level)));
        proto->set_is_packed(inode.is_packed);
        proto->set_parent_id(inode.parent_id);
        proto->set_name(inode.name);
    }

    void MetaServiceImpl::FillDentryProto(const DirEntry &entry, DentryProto *proto)
    {
        proto->set_name(entry.name);
        proto->set_inode_id(entry.inode_id);
        if (entry.file_type == openfs::InodeType::kDirectory)
        {
            proto->set_file_type(FILE_TYPE_DIRECTORY);
        }
        else
        {
            proto->set_file_type(FILE_TYPE_REGULAR);
        }
    }

    // ---- File operations ----
    grpc::Status MetaServiceImpl::CreateFile(grpc::ServerContext *ctx,
                                             const CreateFileReq *req,
                                             CreateFileResp *resp)
    {
        LOG_DEBUG("CreateFile: path={}", req->path());
        Inode out;
        Status s = namespace_mgr_.CreateFile(req->path(), req->mode(),
                                             req->uid(), req->gid(),
                                             req->file_size(), out);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            FillInodeProto(out, resp->mutable_inode());
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::DeleteFile(grpc::ServerContext *ctx,
                                             const DeleteFileReq *req,
                                             DeleteFileResp *resp)
    {
        LOG_DEBUG("DeleteFile: path={}", req->path());
        Status s = namespace_mgr_.DeleteFile(req->path());
        resp->set_status(static_cast<int32_t>(s));
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::GetFileInfo(grpc::ServerContext *ctx,
                                              const GetFileInfoReq *req,
                                              GetFileInfoResp *resp)
    {
        LOG_DEBUG("GetFileInfo: path={}", req->path());
        Inode out;
        Status s = namespace_mgr_.Lookup(req->path(), out);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            FillInodeProto(out, resp->mutable_inode());
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::Rename(grpc::ServerContext *ctx,
                                         const RenameReq *req,
                                         RenameResp *resp)
    {
        LOG_DEBUG("Rename: {} -> {}", req->src_path(), req->dst_path());
        Status s = namespace_mgr_.Rename(req->src_path(), req->dst_path());
        resp->set_status(static_cast<int32_t>(s));
        return grpc::Status::OK;
    }

    // ---- Directory operations ----
    grpc::Status MetaServiceImpl::MkDir(grpc::ServerContext *ctx,
                                        const MkDirReq *req,
                                        MkDirResp *resp)
    {
        LOG_DEBUG("MkDir: path={}", req->path());
        Inode out;
        Status s = namespace_mgr_.MkDir(req->path(), req->mode(),
                                        req->uid(), req->gid(), out);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            FillInodeProto(out, resp->mutable_inode());
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::ReadDir(grpc::ServerContext *ctx,
                                          const ReadDirReq *req,
                                          ReadDirResp *resp)
    {
        LOG_DEBUG("ReadDir: path={}", req->path());
        std::vector<DirEntry> entries;
        Status s = namespace_mgr_.ReadDir(req->path(), entries);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            for (const auto &e : entries)
            {
                FillDentryProto(e, resp->add_entries());
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::RmDir(grpc::ServerContext *ctx,
                                        const RmDirReq *req,
                                        RmDirResp *resp)
    {
        LOG_DEBUG("RmDir: path={}", req->path());
        Status s = namespace_mgr_.RmDir(req->path());
        resp->set_status(static_cast<int32_t>(s));
        return grpc::Status::OK;
    }

    // ---- Block allocation & commit (stubs for Phase 1) ----
    grpc::Status MetaServiceImpl::AllocateBlocks(grpc::ServerContext *ctx,
                                                 const AllocBlocksReq *req,
                                                 AllocBlocksResp *resp)
    {
        LOG_WARN("AllocateBlocks: not fully implemented yet");
        resp->set_status(static_cast<int32_t>(Status::kOk));
        // TODO: allocate block IDs, pick data nodes, return BlockMetaProto
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::CommitBlocks(grpc::ServerContext *ctx,
                                               const CommitBlocksReq *req,
                                               CommitBlocksResp *resp)
    {
        LOG_WARN("CommitBlocks: not fully implemented yet");
        resp->set_status(static_cast<int32_t>(Status::kOk));
        // TODO: commit block metadata after DataNode confirms write
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::GetBlockLocations(grpc::ServerContext *ctx,
                                                    const GetBlockLocsReq *req,
                                                    GetBlockLocsResp *resp)
    {
        LOG_WARN("GetBlockLocations: not fully implemented yet");
        resp->set_status(static_cast<int32_t>(Status::kOk));
        // TODO: return block locations for a given inode
        return grpc::Status::OK;
    }

} // namespace openfs