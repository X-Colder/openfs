#include "s3/s3_server.h"
#include "common/logging.h"

namespace openfs
{

    S3Server::S3Server(const ClientConfig &config)
        : client_()
    {
        client_.Init(config);
    }

    S3Server::~S3Server()
    {
        Stop();
    }

    Status S3Server::Start(const std::string &listen_addr)
    {
        listen_addr_ = listen_addr;
        running_ = true;

        // TODO: Full HTTP server implementation.
        // Production would use brpc's built-in HTTP server or a lightweight
        // HTTP library. For Phase 6, we provide the S3 operation handlers.
        //
        // HTTP server skeleton:
        // 1. Create TCP socket, bind to listen_addr
        // 2. Accept connections in a loop
        // 3. Parse HTTP request (method + path + headers + body)
        // 4. Route to HandleRequest()
        // 5. Send HTTP response
        LOG_INFO("S3 Gateway started at {} (stub - HTTP server not yet implemented)", listen_addr);
        return Status::kOk;
    }

    Status S3Server::Stop()
    {
        running_ = false;
        LOG_INFO("S3 Gateway stopped");
        return Status::kOk;
    }

    S3Server::HttpResponse S3Server::HandleRequest(const HttpRequest &req)
    {
        // Parse bucket and key from path: /bucket/key or /bucket
        std::string path = req.path;
        if (!path.empty() && path[0] == '/')
            path = path.substr(1);

        size_t slash = path.find('/');
        std::string bucket = path.substr(0, slash);
        std::string key = (slash != std::string::npos) ? path.substr(slash + 1) : "";

        if (bucket.empty())
        {
            // List all buckets
            HttpResponse resp;
            resp.status_code = 200;
            resp.body = "<?xml version=\"1.0\"?><ListAllMyBucketsResult></ListAllMyBucketsResult>";
            return resp;
        }

        if (req.method == "PUT")
        {
            if (key.empty())
                return HandleCreateBucket(bucket);
            else
                return HandlePutObject(bucket, key, req.body);
        }
        else if (req.method == "GET")
        {
            if (key.empty())
                return HandleListObjects(bucket);
            else
                return HandleGetObject(bucket, key);
        }
        else if (req.method == "DELETE")
        {
            if (key.empty())
                return HandleDeleteBucket(bucket);
            else
                return HandleDeleteObject(bucket, key);
        }
        else if (req.method == "HEAD")
        {
            return HandleHeadObject(bucket, key);
        }

        HttpResponse resp;
        resp.status_code = 405; // Method Not Allowed
        return resp;
    }

    S3Server::HttpResponse S3Server::HandlePutObject(const std::string &bucket, const std::string &key,
                                                     const std::string &data)
    {
        std::string path = "/" + bucket + "/" + key;
        Status s = client_.WriteFile(path, 0644, data.data(), data.size());

        HttpResponse resp;
        if (s == Status::kOk)
        {
            resp.status_code = 200;
        }
        else
        {
            resp.status_code = 500;
        }
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleGetObject(const std::string &bucket, const std::string &key)
    {
        std::string path = "/" + bucket + "/" + key;
        std::vector<char> data;
        Status s = client_.ReadFile(path, data);

        HttpResponse resp;
        if (s == Status::kOk)
        {
            resp.status_code = 200;
            resp.body.assign(data.begin(), data.end());
            resp.headers["Content-Type"] = "application/octet-stream";
        }
        else if (s == Status::kNotFound)
        {
            resp.status_code = 404;
        }
        else
        {
            resp.status_code = 500;
        }
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleDeleteObject(const std::string &bucket, const std::string &key)
    {
        std::string path = "/" + bucket + "/" + key;
        Status s = client_.DeleteFile(path);

        HttpResponse resp;
        resp.status_code = (s == Status::kOk) ? 204 : 500;
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleHeadObject(const std::string &bucket, const std::string &key)
    {
        std::string path = "/" + bucket + "/" + key;
        Inode inode;
        Status s = client_.GetFileInfo(path, inode);

        HttpResponse resp;
        if (s == Status::kOk)
        {
            resp.status_code = 200;
            resp.headers["Content-Length"] = std::to_string(inode.size);
            resp.headers["Last-Modified"] = std::to_string(inode.mtime_ns / 1000000000);
        }
        else if (s == Status::kNotFound)
        {
            resp.status_code = 404;
        }
        else
        {
            resp.status_code = 500;
        }
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleListObjects(const std::string &bucket)
    {
        std::string path = "/" + bucket;
        std::vector<DirEntry> entries;
        Status s = client_.ReadDir(path, entries);

        HttpResponse resp;
        if (s == Status::kOk)
        {
            resp.status_code = 200;
            // Simple XML listing
            std::string xml = "<?xml version=\"1.0\"?><ListBucketResult>";
            for (const auto &e : entries)
            {
                xml += "<Contents><Key>" + e.name + "</Key></Contents>";
            }
            xml += "</ListBucketResult>";
            resp.body = xml;
            resp.headers["Content-Type"] = "application/xml";
        }
        else
        {
            resp.status_code = 500;
        }
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleCreateBucket(const std::string &bucket)
    {
        std::string path = "/" + bucket;
        Inode inode;
        Status s = client_.MkDir(path, 0755, inode);

        HttpResponse resp;
        resp.status_code = (s == Status::kOk) ? 200 : 500;
        return resp;
    }

    S3Server::HttpResponse S3Server::HandleDeleteBucket(const std::string &bucket)
    {
        std::string path = "/" + bucket;
        Status s = client_.RmDir(path);

        HttpResponse resp;
        resp.status_code = (s == Status::kOk) ? 204 : 500;
        return resp;
    }

} // namespace openfs