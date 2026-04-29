#pragma once

#include <string>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <process.h>
#define GET_PID() _getpid()
#else
#define GET_PID() ::getpid()
#endif

namespace openfs
{
    namespace test
    {
        // Initialize logging for tests (suppress info/debug)
        void InitTestLogging();

        // Create a unique temporary directory for test isolation
        inline std::string MakeTempDir(const std::string &prefix = "openfs_test")
        {
            std::string path = std::filesystem::temp_directory_path().string() + "/" + prefix + "_" + std::to_string(GET_PID()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&path));
            std::filesystem::create_directories(path);
            return path;
        }

        // RAII helper to clean up a temporary directory
        class TempDirGuard
        {
        public:
            explicit TempDirGuard(const std::string &path) : path_(path) {}
            ~TempDirGuard() { std::filesystem::remove_all(path_); }
            const std::string &Path() const { return path_; }

            TempDirGuard(const TempDirGuard &) = delete;
            TempDirGuard &operator=(const TempDirGuard &) = delete;

        private:
            std::string path_;
        };
    } // namespace test
} // namespace openfs