#include "client/openfs_client.h"
#include "common/config.h"
#include "common/logging.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <fstream>

using namespace openfs;

static void PrintUsage()
{
    std::cout << "OpenFS Command Line Tool\n"
              << "\n"
              << "Usage: openfs-cli <command> [args...]\n"
              << "\n"
              << "File operations:\n"
              << "  ls <path>                  List directory\n"
              << "  stat <path>                Get file info\n"
              << "  mkdir <path>               Create directory\n"
              << "  rmdir <path>               Remove directory\n"
              << "  rm <path>                  Delete file\n"
              << "  rename <src> <dst>         Rename file\n"
              << "  put <local> <remote>       Upload local file to OpenFS\n"
              << "  get <remote> <local>       Download file from OpenFS\n"
              << "\n"
              << "Cluster management:\n"
              << "  cluster status             Show cluster status\n"
              << "  cluster nodes              List DataNodes\n"
              << "\n"
              << "Cache management:\n"
              << "  cache stat                 Show cache statistics\n"
              << "  cache warmup <path>        Warm up cache for path\n"
              << "  cache evict <path>         Evict cache for path\n"
              << "\n"
              << "Diagnostics:\n"
              << "  check block <block_id>     Check block integrity\n"
              << "  fsck                       File system check\n"
              << "\n"
              << "Options:\n"
              << "  --meta <addr>              MetaNode address (default: localhost:8100)\n"
              << "  --data <addr>              DataNode address (default: localhost:8200)\n";
}

static ClientConfig g_config;

static int CmdLs(OpenFSClient &client, const std::vector<std::string> &args)
{
    std::string path = args.empty() ? "/" : args[0];
    std::vector<DirEntry> entries;
    Status s = client.ReadDir(path, entries);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    for (const auto &e : entries)
    {
        char type = (e.file_type == InodeType::kDirectory) ? 'd' : '-';
        std::cout << type << " " << e.name << "\n";
    }
    return 0;
}

static int CmdStat(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: stat <path>" << std::endl;
        return 1;
    }
    Inode inode;
    Status s = client.GetFileInfo(args[0], inode);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "  inode_id: " << inode.inode_id << "\n"
              << "  type: " << (inode.file_type == InodeType::kDirectory ? "directory" : "file") << "\n"
              << "  size: " << inode.size << "\n"
              << "  mode: " << std::oct << inode.mode << std::dec << "\n"
              << "  nlink: " << inode.nlink << "\n"
              << "  block_level: L" << static_cast<int>(inode.block_level) << "\n";
    return 0;
}

static int CmdMkdir(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: mkdir <path>" << std::endl;
        return 1;
    }
    Inode inode;
    Status s = client.MkDir(args[0], 0755, inode);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "Created directory: " << args[0] << " (inode=" << inode.inode_id << ")" << std::endl;
    return 0;
}

static int CmdRmdir(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: rmdir <path>" << std::endl;
        return 1;
    }
    Status s = client.RmDir(args[0]);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "Removed directory: " << args[0] << std::endl;
    return 0;
}

static int CmdRm(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: rm <path>" << std::endl;
        return 1;
    }
    Status s = client.DeleteFile(args[0]);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "Deleted: " << args[0] << std::endl;
    return 0;
}

static int CmdRename(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: rename <src> <dst>" << std::endl;
        return 1;
    }
    Status s = client.Rename(args[0], args[1]);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "Renamed: " << args[0] << " -> " << args[1] << std::endl;
    return 0;
}

static int CmdPut(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: put <local> <remote>" << std::endl;
        return 1;
    }
    // Read local file
    std::ifstream file(args[0], std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::cerr << "Error: cannot open local file: " << args[0] << std::endl;
        return 1;
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data(size);
    file.read(data.data(), size);

    Status s = client.WriteFile(args[1], 0644, data.data(), data.size());
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::cout << "Uploaded: " << args[0] << " -> " << args[1] << " (" << size << " bytes)" << std::endl;
    return 0;
}

static int CmdGet(OpenFSClient &client, const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: get <remote> <local>" << std::endl;
        return 1;
    }
    std::vector<char> data;
    Status s = client.ReadFile(args[0], data);
    if (s != Status::kOk)
    {
        std::cerr << "Error: " << StatusToString(s) << std::endl;
        return 1;
    }
    std::ofstream file(args[1], std::ios::binary);
    file.write(data.data(), data.size());
    std::cout << "Downloaded: " << args[0] << " -> " << args[1] << " (" << data.size() << " bytes)" << std::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    InitLogging("openfs_cli");

    // Parse global options
    ClientConfig config;
    config.meta_addr = "localhost:8100";

    std::vector<std::string> cmd_args;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--meta") == 0 && i + 1 < argc)
        {
            config.meta_addr = argv[++i];
        }
        else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
        {
            // DataNode address hint (stored in config for future use)
            ++i;
        }
        else
        {
            cmd_args.push_back(argv[i]);
        }
    }

    if (cmd_args.empty())
    {
        PrintUsage();
        return 1;
    }

    OpenFSClient client;
    Status s = client.Init(config);
    if (s != Status::kOk)
    {
        std::cerr << "Failed to initialize client: " << StatusToString(s) << std::endl;
        return 1;
    }

    const std::string &cmd = cmd_args[0];
    std::vector<std::string> args(cmd_args.begin() + 1, cmd_args.end());

    if (cmd == "ls")
        return CmdLs(client, args);
    else if (cmd == "stat")
        return CmdStat(client, args);
    else if (cmd == "mkdir")
        return CmdMkdir(client, args);
    else if (cmd == "rmdir")
        return CmdRmdir(client, args);
    else if (cmd == "rm")
        return CmdRm(client, args);
    else if (cmd == "rename")
        return CmdRename(client, args);
    else if (cmd == "put")
        return CmdPut(client, args);
    else if (cmd == "get")
        return CmdGet(client, args);
    else if (cmd == "cluster")
    {
        if (args.empty() || args[0] == "status")
        {
            std::cout << "Cluster status: (requires admin service connection)\n";
            return 0;
        }
        else if (args[0] == "nodes")
        {
            std::cout << "Node list: (requires admin service connection)\n";
            return 0;
        }
    }
    else if (cmd == "cache")
    {
        if (args.empty() || args[0] == "stat")
        {
            std::cout << "Cache stats: (requires cache service connection)\n";
            return 0;
        }
        else if (args[0] == "warmup")
        {
            std::cout << "Cache warmup: (requires cache service connection)\n";
            return 0;
        }
        else if (args[0] == "evict")
        {
            std::cout << "Cache evict: (requires cache service connection)\n";
            return 0;
        }
    }
    else if (cmd == "fsck")
    {
        std::cout << "Filesystem check: (requires admin service connection)\n";
        return 0;
    }
    else
    {
        std::cerr << "Unknown command: " << cmd << std::endl;
        PrintUsage();
        return 1;
    }

    return 0;
}