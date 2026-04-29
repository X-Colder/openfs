#include "data/block_io_engine.h"
#include "common/logging.h"
#include <cstring>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#endif

namespace openfs
{

    BlockIOEngine::~BlockIOEngine()
    {
        Close();
    }

    // ============================================================
    // Check if a path looks like a raw device (Linux)
    // ============================================================
    static bool IsRawDevice(const std::string &path)
    {
#ifdef __linux__
        // Raw device paths typically start with /dev/
        if (path.compare(0, 5, "/dev/") == 0)
            return true;
        // Also check if the path is a block device via stat
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISBLK(st.st_mode))
            return true;
#endif
        return false;
    }

    // ============================================================
    // Open
    // ============================================================
    Status BlockIOEngine::Open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = path;

#ifdef __linux__
        // Try O_DIRECT for raw devices or when path starts with /dev/
        if (IsRawDevice(path))
        {
            fd_ = ::open(path.c_str(), O_RDWR | O_DIRECT | O_CLOEXEC);
            if (fd_ < 0)
            {
                // O_DIRECT might fail if filesystem doesn't support it,
                // try without O_DIRECT
                LOG_WARN("O_DIRECT open failed for {} (errno={}), trying without O_DIRECT",
                         path, errno);
                fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
                if (fd_ < 0)
                {
                    LOG_ERROR("Failed to open device {}: errno={}", path, errno);
                    return Status::kIOError;
                }
            }
            else
            {
                use_direct_io_ = true;
            }
            LOG_INFO("Opened block device: {} (direct_io={}, fd={})",
                     path, use_direct_io_, fd_);
            return Status::kOk;
        }
#endif

        // File-based disk: use fstream (works on all platforms)
        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!file_.is_open())
        {
            LOG_ERROR("Failed to open block device: {}", path_);
            return Status::kIOError;
        }

        LOG_INFO("Opened block device: {}", path_);
        return Status::kOk;
    }

    // ============================================================
    // Close
    // ============================================================
    void BlockIOEngine::Close()
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __linux__
        if (fd_ >= 0)
        {
            ::fsync(fd_);
            ::close(fd_);
            fd_ = -1;
            use_direct_io_ = false;
            LOG_INFO("Closed block device (fd): {}", path_);
            return;
        }
#endif

        if (file_.is_open())
        {
            file_.flush();
            file_.close();
            LOG_INFO("Closed block device (fstream): {}", path_);
        }
    }

    // ============================================================
    // ReadBlocks / WriteBlocks (high-level, by block index)
    // ============================================================
    Status BlockIOEngine::ReadBlocks(uint64_t block_index, uint32_t num_blocks,
                                     std::vector<char> &out_data)
    {
        uint64_t offset = block_index * kPhysicalBlockSize;
        uint32_t size = num_blocks * kPhysicalBlockSize;
        return ReadAt(offset, size, out_data);
    }

    Status BlockIOEngine::WriteBlocks(uint64_t block_index, uint32_t /*num_blocks*/,
                                      const void *data, uint32_t data_size)
    {
        uint64_t offset = block_index * kPhysicalBlockSize;
        return WriteAt(offset, data, data_size);
    }

    // ============================================================
    // ReadAt - dispatches to fstream or direct IO
    // ============================================================
    Status BlockIOEngine::ReadAt(uint64_t offset, uint32_t size, std::vector<char> &out_data)
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __linux__
        if (use_direct_io_ && fd_ >= 0)
        {
            // Unlock and call the direct IO version (which doesn't lock)
            mutex_.unlock();
            return ReadAtDirect(offset, size, out_data);
        }
#endif

        // fstream path
        if (!file_.is_open())
            return Status::kIOError;

        file_.seekg(static_cast<std::streamoff>(offset));
        if (!file_)
        {
            LOG_ERROR("Failed to seek to offset {} in {}", offset, path_);
            return Status::kIOError;
        }

        out_data.resize(size);
        file_.read(out_data.data(), size);
        if (!file_)
        {
            LOG_ERROR("Failed to read {} bytes at offset {} in {}", size, offset, path_);
            out_data.clear();
            return Status::kIOError;
        }

        return Status::kOk;
    }

    // ============================================================
    // WriteAt - dispatches to fstream or direct IO
    // ============================================================
    Status BlockIOEngine::WriteAt(uint64_t offset, const void *data, uint32_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __linux__
        if (use_direct_io_ && fd_ >= 0)
        {
            mutex_.unlock();
            return WriteAtDirect(offset, data, size);
        }
#endif

        // fstream path
        if (!file_.is_open())
            return Status::kIOError;

        file_.seekp(static_cast<std::streamoff>(offset));
        if (!file_)
        {
            LOG_ERROR("Failed to seek to offset {} in {}", offset, path_);
            return Status::kIOError;
        }

        file_.write(static_cast<const char *>(data), size);
        if (!file_)
        {
            LOG_ERROR("Failed to write {} bytes at offset {} in {}", size, offset, path_);
            return Status::kIOError;
        }

        file_.flush();
        return Status::kOk;
    }

    // ============================================================
    // Sync - flush to persistent storage
    // ============================================================
    Status BlockIOEngine::Sync()
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __linux__
        if (fd_ >= 0)
        {
            if (::fsync(fd_) != 0)
            {
                LOG_ERROR("fsync failed for {}: errno={}", path_, errno);
                return Status::kIOError;
            }
            return Status::kOk;
        }
#endif

        if (file_.is_open())
        {
            file_.flush();
#ifdef __linux__
            // On Linux, also fsync the file descriptor for durability
            // Note: this is a best-effort; fstream doesn't expose fd easily
            // For production, the fd-based path should be used instead
#endif
        }

        return Status::kOk;
    }

    // ============================================================
    // IsOpen
    // ============================================================
    bool BlockIOEngine::IsOpen() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
#ifdef __linux__
        if (fd_ >= 0)
            return true;
#endif
        return file_.is_open();
    }

    // ============================================================
    // GetFileSize
    // ============================================================
    uint64_t BlockIOEngine::GetFileSize() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __linux__
        if (fd_ >= 0)
        {
            struct stat st;
            if (::fstat(fd_, &st) == 0)
                return static_cast<uint64_t>(st.st_size);
            return 0;
        }
#endif

        if (!file_.is_open())
            return 0;

        auto pos = file_.tellg();
        file_.seekg(0, std::ios::end);
        auto size = file_.tellg();
        file_.seekg(pos);
        return static_cast<uint64_t>(size);
    }

    // ============================================================
    // Linux O_DIRECT read/write implementations
    // ============================================================
#ifdef __linux__

    void *BlockIOEngine::AllocAligned(size_t size)
    {
        void *ptr = nullptr;
        if (posix_memalign(&ptr, kAlignSize, size) != 0)
            return nullptr;
        return ptr;
    }

    void BlockIOEngine::FreeAligned(void *ptr)
    {
        free(ptr);
    }

    Status BlockIOEngine::ReadAtDirect(uint64_t offset, uint32_t size, std::vector<char> &out_data)
    {
        // O_DIRECT requires aligned buffers and aligned offsets/sizes
        // Round up size to alignment boundary
        size_t aligned_size = ((size + kAlignSize - 1) / kAlignSize) * kAlignSize;

        void *buf = AllocAligned(aligned_size);
        if (!buf)
        {
            LOG_ERROR("Failed to allocate aligned buffer for read");
            return Status::kIOError;
        }

        ssize_t ret = ::pread(fd_, buf, aligned_size, static_cast<off_t>(offset));
        if (ret < 0)
        {
            LOG_ERROR("pread failed for {} at offset {}: errno={}", path_, offset, errno);
            FreeAligned(buf);
            return Status::kIOError;
        }

        out_data.assign(static_cast<char *>(buf), static_cast<char *>(buf) + size);
        FreeAligned(buf);
        return Status::kOk;
    }

    Status BlockIOEngine::WriteAtDirect(uint64_t offset, const void *data, uint32_t size)
    {
        // O_DIRECT requires aligned buffers
        size_t aligned_size = ((size + kAlignSize - 1) / kAlignSize) * kAlignSize;

        void *buf = AllocAligned(aligned_size);
        if (!buf)
        {
            LOG_ERROR("Failed to allocate aligned buffer for write");
            return Status::kIOError;
        }

        std::memcpy(buf, data, size);
        // Zero-fill remaining aligned space
        if (size < aligned_size)
            std::memset(static_cast<char *>(buf) + size, 0, aligned_size - size);

        ssize_t ret = ::pwrite(fd_, buf, aligned_size, static_cast<off_t>(offset));
        FreeAligned(buf);

        if (ret < 0)
        {
            LOG_ERROR("pwrite failed for {} at offset {}: errno={}", path_, offset, errno);
            return Status::kIOError;
        }

        return Status::kOk;
    }

#endif // __linux__

} // namespace openfs