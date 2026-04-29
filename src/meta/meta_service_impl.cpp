#include "meta/meta_service_impl.h"
#include "common/logging.h"
#include "common/id_generator.h"

// Undefine Windows macros that conflict with our internal method names
// (NamespaceManager::CreateFile, NamespaceManager::DeleteFile)
// These macros are defined in <windows.h> which is indirectly included by gRPC/protobuf
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef DeleteFile
#undef DeleteFile
#endif

namespace openfs
{

    // ============================================================
    // NodeServiceImpl
    // ============================================================

    NodeServiceImpl::NodeServiceImpl(NodeManager &node_mgr, BlockMap &block_map)
        : node_mgr_(node_mgr), block_map_(block_map) {}

    grpc::Status NodeServiceImpl::Register(grpc::ServerContext *ctx,
                                           const RegisterReq *req,
                                           RegisterResp *resp)
    {
        LOG_INFO("NodeService::Register: address={}, capacity={}",
                 req->address(), req->capacity());

        uint64_t node_id = node_mgr_.RegisterNode(req->address(), req->capacity());
        resp->set_status(static_cast<int32_t>(Status::kOk));
        resp->set_node_id(node_id);

        LOG_INFO("Registered DataNode {} at {} with capacity {}",
                 node_id, req->address(), req->capacity());
        return grpc::Status::OK;
    }

    grpc::Status NodeServiceImpl::Heartbeat(grpc::ServerContext *ctx,
                                            const HeartbeatReq *req,
                                            HeartbeatResp *resp)
    {
        LOG_DEBUG("NodeService::Heartbeat: node_id={}, block_count={}, cpu_load={}",
                  req->node_id(), req->block_count(), req->cpu_load());

        // Calculate total used from disk usage
        uint64_t total_used = 0;
        for (const auto &disk : req->disk_usage())
        {
            total_used += disk.used();
        }

        node_mgr_.UpdateHeartbeat(req->node_id(), total_used);
        resp->set_status(static_cast<int32_t>(Status::kOk));
        // TODO: check for blocks that need to be deleted/migrated
        return grpc::Status::OK;
    }

    grpc::Status NodeServiceImpl::ReportBlocks(grpc::ServerContext *ctx,
                                               const ReportBlocksReq *req,
                                               ReportBlocksResp *resp)
    {
        LOG_DEBUG("NodeService::ReportBlocks: node_id={}, block_count={}",
                  req->node_id(), req->block_ids_size());

        // TODO: reconcile reported blocks with BlockMap metadata
        resp->set_status(static_cast<int32_t>(Status::kOk));
        return grpc::Status::OK;
    }

    // ============================================================
    // MetaServiceImpl
    // ============================================================

    MetaServiceImpl::MetaServiceImpl()
        : namespace_mgr_(inode_table_),
          block_allocator_(node_mgr_) {}

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

    void MetaServiceImpl::FillBlockMetaProto(const BlockMeta &block, BlockMetaProto *proto)
    {
        proto->set_block_id(block.block_id);
        proto->set_level(static_cast<::openfs::BlockLevel>(static_cast<int>(block.level)));
        proto->set_size(block.size);
        proto->set_crc32(block.crc32);
        proto->set_node_id(block.node_id);
        proto->set_segment_id(block.segment_id);
        proto->set_offset(block.offset);
        proto->set_create_time(block.create_time);
        proto->set_replica_count(block.replica_count);
        proto->set_access_count(block.access_count);
    }

    // ---- File operations ----
    grpc::Status MetaServiceImpl::CreateFsFile(grpc::ServerContext *ctx,
                                               const CreateFsFileReq *req,
                                               CreateFsFileResp *resp)
    {
        LOG_DEBUG("CreateFsFile: path={}", req->path());
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

    grpc::Status MetaServiceImpl::RemoveFsFile(grpc::ServerContext *ctx,
                                               const RemoveFsFileReq *req,
                                               RemoveFsFileResp *resp)
    {
        LOG_DEBUG("RemoveFsFile: path={}", req->path());
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

    // ---- Block allocation & commit ----
    grpc::Status MetaServiceImpl::AllocateBlocks(grpc::ServerContext *ctx,
                                                 const AllocBlocksReq *req,
                                                 AllocBlocksResp *resp)
    {
        LOG_DEBUG("AllocateBlocks: inode_id={}, block_count={}, level={}",
                  req->inode_id(), req->block_count(),
                  static_cast<int>(req->level()));

        std::vector<BlockMeta> blocks;
        BlkLevel level = static_cast<BlkLevel>(static_cast<int>(req->level()));
        Status s = block_allocator_.Allocate(req->block_count(), level, blocks);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            for (const auto &blk : blocks)
            {
                FillBlockMetaProto(blk, resp->add_blocks());
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::CommitBlocks(grpc::ServerContext *ctx,
                                               const CommitBlocksReq *req,
                                               CommitBlocksResp *resp)
    {
        LOG_DEBUG("CommitBlocks: inode_id={}, block_count={}",
                  req->inode_id(), req->blocks_size());

        std::vector<BlockMeta> blocks;
        blocks.reserve(req->blocks_size());
        for (const auto &bp : req->blocks())
        {
            BlockMeta bm;
            bm.block_id = bp.block_id();
            bm.level = static_cast<BlkLevel>(static_cast<int>(bp.level()));
            bm.size = bp.size();
            bm.crc32 = bp.crc32();
            bm.node_id = bp.node_id();
            bm.segment_id = bp.segment_id();
            bm.offset = bp.offset();
            bm.create_time = bp.create_time();
            bm.replica_count = bp.replica_count();
            bm.access_count = bp.access_count();
            blocks.push_back(bm);
        }

        Status s = block_map_.AddBlocks(req->inode_id(), blocks);
        resp->set_status(static_cast<int32_t>(s));

        // Update inode size based on committed blocks
        if (s == Status::kOk)
        {
            Inode inode;
            if (inode_table_.Get(req->inode_id(), inode) == Status::kOk)
            {
                uint64_t total_size = 0;
                for (const auto &blk : blocks)
                {
                    total_size += blk.size;
                }
                inode.size = total_size;
                inode.mtime_ns = NowNs();
                inode.ctime_ns = NowNs();
                inode_table_.Update(inode);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status MetaServiceImpl::GetBlockLocations(grpc::ServerContext *ctx,
                                                    const GetBlockLocsReq *req,
                                                    GetBlockLocsResp *resp)
    {
        LOG_DEBUG("GetBlockLocations: inode_id={}", req->inode_id());

        std::vector<BlockMeta> blocks;
        Status s = block_map_.GetBlocks(req->inode_id(), blocks);
        resp->set_status(static_cast<int32_t>(s));
        if (s == Status::kOk)
        {
            for (const auto &blk : blocks)
            {
                FillBlockMetaProto(blk, resp->add_blocks());
            }
        }
        return grpc::Status::OK;
    }

} // namespace openfs