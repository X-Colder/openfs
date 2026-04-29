#include <gtest/gtest.h>
#include "common/config.h"
#include <fstream>
#include <filesystem>

using namespace openfs;

namespace
{
    // Helper: write a config file and return its path
    std::string WriteConfigFile(const std::string &content, const std::string &name)
    {
        std::string path = std::filesystem::temp_directory_path().string() + "/" + name;
        std::ofstream out(path);
        out << content;
        out.close();
        return path;
    }
} // namespace

// ============================================================
// Config loading - MetaNode
// ============================================================
TEST(ConfigTest, LoadMetaNodeConfig)
{
    std::string content =
        "meta.listen_addr=0.0.0.0:9999\n"
        "meta.data_dir=/tmp/test_meta\n"
        "meta.node_id=5\n"
        "meta.raft_peers=host1:8100,host2:8100\n";
    std::string path = WriteConfigFile(content, "test_meta.conf");

    auto &config = Config::Instance();
    ASSERT_TRUE(config.LoadFromFile(path));

    const auto &meta = config.GetMetaConfig();
    EXPECT_EQ(meta.listen_addr, "0.0.0.0:9999");
    EXPECT_EQ(meta.data_dir, "/tmp/test_meta");
    EXPECT_EQ(meta.node_id, 5);
    EXPECT_EQ(meta.raft_peers, "host1:8100,host2:8100");

    std::filesystem::remove(path);
}

// ============================================================
// Config loading - DataNode
// ============================================================
TEST(ConfigTest, LoadDataNodeConfig)
{
    std::string content =
        "data.listen_addr=0.0.0.0:8888\n"
        "data.data_dir=/tmp/test_data\n"
        "data.meta_addr=10.0.0.1:50050\n"
        "data.segment_size=536870912\n"
        "data.max_segments=512\n";
    std::string path = WriteConfigFile(content, "test_data.conf");

    auto &config = Config::Instance();
    ASSERT_TRUE(config.LoadFromFile(path));

    const auto &data = config.GetDataConfig();
    EXPECT_EQ(data.listen_addr, "0.0.0.0:8888");
    EXPECT_EQ(data.data_dir, "/tmp/test_data");
    EXPECT_EQ(data.meta_addr, "10.0.0.1:50050");
    EXPECT_EQ(data.segment_size, 536870912ull);
    EXPECT_EQ(data.max_segments, 512u);

    std::filesystem::remove(path);
}

// ============================================================
// Config loading - missing file
// ============================================================
TEST(ConfigTest, LoadMissingFile_ReturnsFalse)
{
    auto &config = Config::Instance();
    EXPECT_FALSE(config.LoadFromFile("/nonexistent/path/config.conf"));
}

// ============================================================
// Config loading - defaults when keys missing
// ============================================================
TEST(ConfigTest, LoadPartialConfig_UsesDefaults)
{
    std::string content = "meta.node_id=3\n";
    std::string path = WriteConfigFile(content, "test_partial.conf");

    auto &config = Config::Instance();
    ASSERT_TRUE(config.LoadFromFile(path));

    const auto &meta = config.GetMetaConfig();
    EXPECT_EQ(meta.node_id, 3);
    // Other fields should retain their defaults
    EXPECT_EQ(meta.listen_addr, "0.0.0.0:8100");

    std::filesystem::remove(path);
}

// ============================================================
// Config loading - comments and blank lines
// ============================================================
TEST(ConfigTest, SkipsCommentsAndBlankLines)
{
    std::string content =
        "# This is a comment\n"
        "\n"
        "meta.listen_addr=1.2.3.4:5000\n"
        "# Another comment\n"
        "meta.node_id=10\n";
    std::string path = WriteConfigFile(content, "test_comments.conf");

    auto &config = Config::Instance();
    ASSERT_TRUE(config.LoadFromFile(path));

    const auto &meta = config.GetMetaConfig();
    EXPECT_EQ(meta.listen_addr, "1.2.3.4:5000");
    EXPECT_EQ(meta.node_id, 10);

    std::filesystem::remove(path);
}

// ============================================================
// Config loading - whitespace trimming
// ============================================================
TEST(ConfigTest, TrimsWhitespace)
{
    std::string content = "  meta.listen_addr  =  0.0.0.0:7777  \n";
    std::string path = WriteConfigFile(content, "test_trim.conf");

    auto &config = Config::Instance();
    ASSERT_TRUE(config.LoadFromFile(path));

    const auto &meta = config.GetMetaConfig();
    EXPECT_EQ(meta.listen_addr, "0.0.0.0:7777");

    std::filesystem::remove(path);
}

// ============================================================
// Default config values
// ============================================================
TEST(ConfigTest, DefaultValues)
{
    MetaNodeConfig meta;
    EXPECT_EQ(meta.listen_addr, "0.0.0.0:8100");
    EXPECT_EQ(meta.data_dir, "/var/lib/openfs/meta");
    EXPECT_EQ(meta.node_id, 0);

    DataNodeConfig data;
    EXPECT_EQ(data.listen_addr, "0.0.0.0:8200");
    EXPECT_EQ(data.segment_size, 256ull * 1024 * 1024);

    ClientConfig client;
    EXPECT_EQ(client.batch_size, 16u);
    EXPECT_EQ(client.rpc_timeout_ms, 30000u);
}