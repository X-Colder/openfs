#include "client/meta_connection.h"
#include "common/logging.h"

namespace openfs
{

    MetaConnection::MetaConnection(const std::string &meta_addr)
        : meta_addr_(meta_addr)
    {
        channel_ = grpc::CreateChannel(meta_addr, grpc::InsecureChannelCredentials());
        stub_ = MetaService::NewStub(channel_);
    }

    Inode MetaConnection::ProtoToInode(const InodeProto &proto)
    {
        Inode inode;
        inode.inode_id = proto.inode_id();
        inode.file_type = static_cast<InodeType>(proto.file_type());
        inode.mode = proto.mode();
        inode.uid = proto.uid();
        inode.gid = proto.gid();
        inode.size = proto.size();
        inode.nlink = proto.nlink();
        inode.atime_ns = proto.atime_ns();
        inode.mtime_ns = proto.mtime_ns();
        inode.ctime_ns = proto.ctime_ns();
        inode.block_level = static_cast<BlkLevel>(proto.block_level());
        inode.is_packed = proto.is_packed();
        inode.parent_id = proto.parent_id();
        inode.name = proto.name();
        return inode;
    }

    void MetaConnection::InodeToProto(const Inode &inode, InodeProto &proto)
    {
        proto.set_inode_id(inode.inode_id);
        proto.set_file_type(static_cast<FileType>(inode.file_type));
        proto.set_mode(inode.mode);
        proto.set_uid(inode.uid);
        proto.set_gid(inode.gid);
        proto.set_size(inode.size);
        proto.set_nlink(inode.nlink);
        proto.set_atime_ns(inode.atime_ns);
        proto.set_mtime_ns(inode.mtime_ns);
        proto.set_ctime_ns(inode.ctime_ns);
        proto.set_block_level(static_cast<BlockLevel>(inode.block_level));
        proto.set_is_packed(inode.is_packed);
        proto.set_parent_id(inode.parent_id);
        proto.set_name(inode.name);
    }

    BlockMeta MetaConnection::ProtoToBlockMeta(const BlockMetaProto &proto)
    {
        BlockMeta meta;
        meta.block_id = proto.block_id();
        meta.level = static_cast<BlkLevel>(proto.level());
        meta.size = proto.size();
        meta.crc32 = proto.crc32();
        meta.node_id = proto.node_id();
        meta.segment_id = proto.segment_id();
        meta.offset = proto.offset();
        meta.create_time = proto.create_time();
        meta.replica_count = proto.replica_count();
        meta.access_count = proto.access_count();
        return meta;
    }

    void MetaConnection::BlockMetaToProto(const BlockMeta &meta, BlockMetaProto &proto)
    {
        proto.set_block_id(meta.block_id);
        proto.set_level(static_cast<BlockLevel>(meta.level));
        proto.set_size(meta.size);
        proto.set_crc32(meta.crc32);
        proto.set_node_id(meta.node_id);
        proto.set_segment_id(meta.segment_id);
        proto.set_offset(meta.offset);
        proto.set_create_time(meta.create_time);
        proto.set_replica_count(meta.replica_count);
        proto.set_access_count(meta.access_count);
    }

    Status MetaConnection::CreateFile(const std::string &path, uint32_t mode,
                                      uint32_t uid, uint32_t gid, uint64_t file_size,
                                      Inode &out_inode)
    {
        grpc::ClientContext ctx;
        CreateFsFileReq req;
        req.set_path(path);
        req.set_mode(mode);
        req.set_uid(uid);
        req.set_gid(gid);
        req.set_file_size(file_size);
        CreateFsFileResp resp;

        auto s = stub_->CreateFsFile(&ctx, req, &resp);
        if (!s.ok())
        {
            LOG_ERROR("CreateFile RPC failed: {}", s.error_message());
            return Status::kInternal;
        }
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        out_inode = ProtoToInode(resp.inode());
        return Status::kOk;
    }

    Status MetaConnection::DeleteFile(const std::string &path)
    {
        grpc::ClientContext ctx;
        RemoveFsFileReq req;
        req.set_path(path);
        RemoveFsFileResp resp;

        auto s = stub_->RemoveFsFile(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        return static_cast<Status>(resp.status());
    }

    Status MetaConnection::GetFileInfo(const std::string &path, Inode &out_inode)
    {
        grpc::ClientContext ctx;
        GetFileInfoReq req;
        req.set_path(path);
        GetFileInfoResp resp;

        auto s = stub_->GetFileInfo(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        out_inode = ProtoToInode(resp.inode());
        return Status::kOk;
    }

    Status MetaConnection::Rename(const std::string &src, const std::string &dst)
    {
        grpc::ClientContext ctx;
        RenameReq req;
        req.set_src_path(src);
        req.set_dst_path(dst);
        RenameResp resp;

        auto s = stub_->Rename(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        return static_cast<Status>(resp.status());
    }

    Status MetaConnection::MkDir(const std::string &path, uint32_t mode,
                                 uint32_t uid, uint32_t gid, Inode &out_inode)
    {
        grpc::ClientContext ctx;
        MkDirReq req;
        req.set_path(path);
        req.set_mode(mode);
        req.set_uid(uid);
        req.set_gid(gid);
        MkDirResp resp;

        auto s = stub_->MkDir(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        out_inode = ProtoToInode(resp.inode());
        return Status::kOk;
    }

    Status MetaConnection::ReadDir(const std::string &path, std::vector<DirEntry> &entries)
    {
        grpc::ClientContext ctx;
        ReadDirReq req;
        req.set_path(path);
        ReadDirResp resp;

        auto s = stub_->ReadDir(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        entries.clear();
        for (const auto &e : resp.entries())
        {
            DirEntry entry;
            entry.name = e.name();
            entry.inode_id = e.inode_id();
            entry.file_type = static_cast<InodeType>(e.file_type());
            entries.push_back(entry);
        }
        return Status::kOk;
    }

    Status MetaConnection::RmDir(const std::string &path)
    {
        grpc::ClientContext ctx;
        RmDirReq req;
        req.set_path(path);
        RmDirResp resp;

        auto s = stub_->RmDir(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        return static_cast<Status>(resp.status());
    }

    Status MetaConnection::AllocateBlocks(uint64_t inode_id, uint32_t block_count,
                                          BlkLevel level, std::vector<BlockMeta> &blocks)
    {
        grpc::ClientContext ctx;
        AllocBlocksReq req;
        req.set_inode_id(inode_id);
        req.set_block_count(block_count);
        req.set_level(static_cast<BlockLevel>(level));
        AllocBlocksResp resp;

        auto s = stub_->AllocateBlocks(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        blocks.clear();
        for (const auto &b : resp.blocks())
            blocks.push_back(ProtoToBlockMeta(b));
        return Status::kOk;
    }

    Status MetaConnection::CommitBlocks(uint64_t inode_id,
                                        const std::vector<BlockMeta> &blocks)
    {
        grpc::ClientContext ctx;
        CommitBlocksReq req;
        req.set_inode_id(inode_id);
        for (const auto &b : blocks)
        {
            auto *proto = req.add_blocks();
            BlockMetaToProto(b, *proto);
        }
        CommitBlocksResp resp;

        auto s = stub_->CommitBlocks(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        return static_cast<Status>(resp.status());
    }

    Status MetaConnection::GetBlockLocations(uint64_t inode_id,
                                             std::vector<BlockMeta> &blocks)
    {
        grpc::ClientContext ctx;
        GetBlockLocsReq req;
        req.set_inode_id(inode_id);
        GetBlockLocsResp resp;

        auto s = stub_->GetBlockLocations(&ctx, req, &resp);
        if (!s.ok())
            return Status::kInternal;
        if (resp.status() != 0)
            return static_cast<Status>(resp.status());

        blocks.clear();
        for (const auto &b : resp.blocks())
            blocks.push_back(ProtoToBlockMeta(b));
        return Status::kOk;
    }

} // namespace openfs