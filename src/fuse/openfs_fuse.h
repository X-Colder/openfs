#pragma once

#include "client/openfs_client.h"
#include "common/config.h"
#include <string>
#include <memory>

namespace openfs
{

    // FUSE adapter: maps POSIX file system operations to OpenFS Client SDK calls.
    // Uses libfuse3 for kernel FUSE interface.
    //
    // Supported operations:
    //   - getattr  → GetFileInfo
    //   - mkdir    → MkDir
    //   - readdir  → ReadDir
    //   - rmdir    → RmDir
    //   - unlink   → DeleteFile
    //   - rename   → Rename
    //   - open     → (check file exists)
    //   - read     → ReadFile
    //   - write    → WriteFile
    //   - create   → CreateFile
    //   - truncate → (rewrite with smaller size)
    //   - statfs   → (report total/free space)
    //
    // Note: This is a header-only interface definition.
    // The actual FUSE lowlevel_ops registration is in openfs_fuse.cpp
    // which requires libfuse3 headers at compile time.

    class OpenFSFuse
    {
    public:
        explicit OpenFSFuse(const ClientConfig &config);
        ~OpenFSFuse();

        // Mount the FUSE file system at the given mount point
        // Blocks until unmounted (Ctrl+C or fusermount -u)
        int Mount(const std::string &mount_point, bool foreground = true);

        // Unmount the FUSE file system
        void Unmount();

        // Get the underlying client (for direct access if needed)
        OpenFSClient &GetClient() { return client_; }

    private:
        OpenFSClient client_;
        std::string mount_point_;
        bool mounted_ = false;

        // FUSE operations are implemented as static callbacks
        // that delegate to the client_ instance.
        // These are registered with fuse_session in Mount().
    };

} // namespace openfs