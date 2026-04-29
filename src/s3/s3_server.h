#pragma once

#include "client/openfs_client.h"
#include "common/config.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace openfs
{

    // S3 Gateway: provides S3-compatible HTTP API on top of OpenFS.
    // Maps S3 operations to OpenFS Client SDK calls.
    //
    // Supported S3 operations:
    //   PUT /bucket/key       → WriteFile
    //   GET /bucket/key       → ReadFile
    //   DELETE /bucket/key    → DeleteFile
    //   HEAD /bucket/key      → GetFileInfo
    //   GET /bucket?list-type=2 → ReadDir
    //   PUT /bucket           → MkDir (create bucket)
    //   DELETE /bucket        → RmDir
    //
    // Note: Requires a built-in HTTP server. For Phase 6,
    // this provides the interface and a simple socket-based HTTP
    // handler. Full production implementation would use brpc's
    // built-in HTTP support.

    class S3Server
    {
    public:
        explicit S3Server(const ClientConfig &config);
        ~S3Server();

        // Start the S3 gateway on the given port
        Status Start(const std::string &listen_addr);

        // Stop the S3 gateway
        Status Stop();

    private:
        // HTTP request handling
        struct HttpRequest
        {
            std::string method; // GET, PUT, DELETE, HEAD
            std::string path;   // e.g., /bucket/key
            std::string body;
            std::unordered_map<std::string, std::string> headers;
        };

        struct HttpResponse
        {
            int status_code = 200;
            std::string body;
            std::unordered_map<std::string, std::string> headers;
        };

        HttpResponse HandleRequest(const HttpRequest &req);
        HttpResponse HandlePutObject(const std::string &bucket, const std::string &key,
                                     const std::string &data);
        HttpResponse HandleGetObject(const std::string &bucket, const std::string &key);
        HttpResponse HandleDeleteObject(const std::string &bucket, const std::string &key);
        HttpResponse HandleHeadObject(const std::string &bucket, const std::string &key);
        HttpResponse HandleListObjects(const std::string &bucket);
        HttpResponse HandleCreateBucket(const std::string &bucket);
        HttpResponse HandleDeleteBucket(const std::string &bucket);

        OpenFSClient client_;
        std::string listen_addr_;
        bool running_ = false;
    };

} // namespace openfs