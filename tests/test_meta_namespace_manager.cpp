#include <gtest/gtest.h>
#include "meta/inode_table.h"
#include "meta/namespace_manager.h"

using namespace openfs;

class NamespaceManagerTest : public ::testing::Test
{
protected:
    InodeTable inode_table;
    NamespaceManager ns_mgr{inode_table};
};

// ============================================================
// Lookup
// ============================================================
TEST_F(NamespaceManagerTest, Lookup_Root)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.Lookup("/", inode), Status::kOk);
    EXPECT_EQ(inode.inode_id, 1u);
    EXPECT_EQ(inode.file_type, InodeType::kDirectory);
}

TEST_F(NamespaceManagerTest, Lookup_NonexistentPath)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.Lookup("/nonexistent", inode), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, Lookup_InvalidPath_NoLeadingSlash)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.Lookup("no_slash", inode), Status::kInvalidArgument);
}

TEST_F(NamespaceManagerTest, Lookup_EmptyPath)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.Lookup("", inode), Status::kInvalidArgument);
}

// ============================================================
// MkDir
// ============================================================
TEST_F(NamespaceManagerTest, MkDir_Success)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.MkDir("/data", 0755, 0, 0, inode), Status::kOk);
    EXPECT_EQ(inode.file_type, InodeType::kDirectory);
    EXPECT_EQ(inode.name, "data");
    EXPECT_EQ(inode.parent_id, 1u);

    // Should be findable by path
    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/data", found), Status::kOk);
    EXPECT_EQ(found.inode_id, inode.inode_id);
}

TEST_F(NamespaceManagerTest, MkDir_DuplicatePath_ReturnsAlreadyExists)
{
    Inode inode;
    ns_mgr.MkDir("/data", 0755, 0, 0, inode);
    EXPECT_EQ(ns_mgr.MkDir("/data", 0755, 0, 0, inode), Status::kAlreadyExists);
}

TEST_F(NamespaceManagerTest, MkDir_NestedDirectories)
{
    Inode d1, d2, d3;
    EXPECT_EQ(ns_mgr.MkDir("/level1", 0755, 0, 0, d1), Status::kOk);
    EXPECT_EQ(ns_mgr.MkDir("/level1/level2", 0755, 0, 0, d2), Status::kOk);
    EXPECT_EQ(ns_mgr.MkDir("/level1/level2/level3", 0755, 0, 0, d3), Status::kOk);

    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/level1/level2/level3", found), Status::kOk);
    EXPECT_EQ(found.name, "level3");
}

TEST_F(NamespaceManagerTest, MkDir_ParentNotExist_ReturnsNotFound)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.MkDir("/nonexistent_dir/child", 0755, 0, 0, inode), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, MkDir_Root_ReturnsAlreadyExists)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.MkDir("/", 0755, 0, 0, inode), Status::kAlreadyExists);
}

// ============================================================
// CreateFile
// ============================================================
TEST_F(NamespaceManagerTest, CreateFile_Success)
{
    Inode inode;
    EXPECT_EQ(ns_mgr.CreateFile("/test.txt", 0644, 0, 0, 1024, inode), Status::kOk);
    EXPECT_EQ(inode.file_type, InodeType::kRegular);
    EXPECT_EQ(inode.name, "test.txt");
    EXPECT_EQ(inode.size, 1024u);
    EXPECT_EQ(inode.parent_id, 1u); // root
}

TEST_F(NamespaceManagerTest, CreateFile_DuplicatePath_ReturnsAlreadyExists)
{
    Inode inode;
    ns_mgr.CreateFile("/test.txt", 0644, 0, 0, 0, inode);
    EXPECT_EQ(ns_mgr.CreateFile("/test.txt", 0644, 0, 0, 0, inode), Status::kAlreadyExists);
}

TEST_F(NamespaceManagerTest, CreateFile_InSubdirectory)
{
    Inode dir;
    ns_mgr.MkDir("/data", 0755, 0, 0, dir);

    Inode file;
    EXPECT_EQ(ns_mgr.CreateFile("/data/file.dat", 0644, 0, 0, 4096, file), Status::kOk);
    EXPECT_EQ(file.parent_id, dir.inode_id);
}

TEST_F(NamespaceManagerTest, CreateFile_BlockLevelAutoSelected)
{
    Inode small_file, medium_file, large_file;

    ns_mgr.CreateFile("/small.dat", 0644, 0, 0, 100, small_file);
    EXPECT_EQ(small_file.block_level, BlkLevel::L0);

    ns_mgr.CreateFile("/medium.dat", 0644, 0, 0, 1024 * 1024, medium_file);
    EXPECT_EQ(medium_file.block_level, BlkLevel::L2);

    ns_mgr.CreateFile("/large.dat", 0644, 0, 0, 100 * 1024 * 1024, large_file);
    EXPECT_EQ(large_file.block_level, BlkLevel::L3);
}

// ============================================================
// DeleteFile
// ============================================================
TEST_F(NamespaceManagerTest, DeleteFile_Success)
{
    Inode inode;
    ns_mgr.CreateFile("/to_delete.txt", 0644, 0, 0, 0, inode);
    EXPECT_EQ(ns_mgr.DeleteFile("/to_delete.txt"), Status::kOk);

    // Should no longer be findable
    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/to_delete.txt", found), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, DeleteFile_Nonexistent_ReturnsNotFound)
{
    EXPECT_EQ(ns_mgr.DeleteFile("/nonexistent.txt"), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, DeleteFile_CannotDeleteDirectory)
{
    Inode dir;
    ns_mgr.MkDir("/dir", 0755, 0, 0, dir);
    // DeleteFile only deletes regular files
    EXPECT_EQ(ns_mgr.DeleteFile("/dir"), Status::kNotFound);
}

// ============================================================
// ReadDir
// ============================================================
TEST_F(NamespaceManagerTest, ReadDir_RootEmpty)
{
    std::vector<DirEntry> entries;
    EXPECT_EQ(ns_mgr.ReadDir("/", entries), Status::kOk);
    EXPECT_TRUE(entries.empty());
}

TEST_F(NamespaceManagerTest, ReadDir_WithEntries)
{
    Inode f1, f2, d1;
    ns_mgr.CreateFile("/file1.txt", 0644, 0, 0, 0, f1);
    ns_mgr.CreateFile("/file2.txt", 0644, 0, 0, 0, f2);
    ns_mgr.MkDir("/subdir", 0755, 0, 0, d1);

    std::vector<DirEntry> entries;
    EXPECT_EQ(ns_mgr.ReadDir("/", entries), Status::kOk);
    EXPECT_EQ(entries.size(), 3u);

    // Verify entries contain expected names and types
    bool found_file1 = false, found_file2 = false, found_subdir = false;
    for (const auto &e : entries)
    {
        if (e.name == "file1.txt" && e.file_type == InodeType::kRegular)
            found_file1 = true;
        if (e.name == "file2.txt" && e.file_type == InodeType::kRegular)
            found_file2 = true;
        if (e.name == "subdir" && e.file_type == InodeType::kDirectory)
            found_subdir = true;
    }
    EXPECT_TRUE(found_file1);
    EXPECT_TRUE(found_file2);
    EXPECT_TRUE(found_subdir);
}

TEST_F(NamespaceManagerTest, ReadDir_NonexistentPath)
{
    std::vector<DirEntry> entries;
    EXPECT_EQ(ns_mgr.ReadDir("/nonexistent", entries), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, ReadDir_FilePath_ReturnsNotDirectory)
{
    Inode file;
    ns_mgr.CreateFile("/file.txt", 0644, 0, 0, 0, file);

    std::vector<DirEntry> entries;
    EXPECT_EQ(ns_mgr.ReadDir("/file.txt", entries), Status::kNotDirectory);
}

// ============================================================
// RmDir
// ============================================================
TEST_F(NamespaceManagerTest, RmDir_Success)
{
    Inode dir;
    ns_mgr.MkDir("/empty_dir", 0755, 0, 0, dir);
    EXPECT_EQ(ns_mgr.RmDir("/empty_dir"), Status::kOk);

    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/empty_dir", found), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, RmDir_NonEmpty_ReturnsNotEmpty)
{
    Inode dir, file;
    ns_mgr.MkDir("/nonempty", 0755, 0, 0, dir);
    ns_mgr.CreateFile("/nonempty/file.txt", 0644, 0, 0, 0, file);

    EXPECT_EQ(ns_mgr.RmDir("/nonempty"), Status::kNotEmpty);
}

TEST_F(NamespaceManagerTest, RmDir_Nonexistent_ReturnsNotFound)
{
    EXPECT_EQ(ns_mgr.RmDir("/nonexistent"), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, RmDir_Root_ReturnsInvalidArgument)
{
    EXPECT_EQ(ns_mgr.RmDir("/"), Status::kInvalidArgument);
}

TEST_F(NamespaceManagerTest, RmDir_RegularFile_ReturnsNotFound)
{
    Inode file;
    ns_mgr.CreateFile("/file.txt", 0644, 0, 0, 0, file);
    // RmDir only removes directories
    EXPECT_EQ(ns_mgr.RmDir("/file.txt"), Status::kNotFound);
}

// ============================================================
// Rename
// ============================================================
TEST_F(NamespaceManagerTest, Rename_Success)
{
    Inode file;
    ns_mgr.CreateFile("/old_name.txt", 0644, 0, 0, 0, file);

    EXPECT_EQ(ns_mgr.Rename("/old_name.txt", "/new_name.txt"), Status::kOk);

    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/old_name.txt", found), Status::kNotFound);
    EXPECT_EQ(ns_mgr.Lookup("/new_name.txt", found), Status::kOk);
    EXPECT_EQ(found.name, "new_name.txt");
}

TEST_F(NamespaceManagerTest, Rename_MoveToDifferentDirectory)
{
    Inode dir, file;
    ns_mgr.MkDir("/dir1", 0755, 0, 0, dir);
    ns_mgr.CreateFile("/dir1/file.txt", 0644, 0, 0, 0, file);

    Inode dir2;
    ns_mgr.MkDir("/dir2", 0755, 0, 0, dir2);

    EXPECT_EQ(ns_mgr.Rename("/dir1/file.txt", "/dir2/file.txt"), Status::kOk);

    Inode found;
    EXPECT_EQ(ns_mgr.Lookup("/dir1/file.txt", found), Status::kNotFound);
    EXPECT_EQ(ns_mgr.Lookup("/dir2/file.txt", found), Status::kOk);
    EXPECT_EQ(found.parent_id, dir2.inode_id);
}

TEST_F(NamespaceManagerTest, Rename_SourceNotExist_ReturnsNotFound)
{
    EXPECT_EQ(ns_mgr.Rename("/nonexistent", "/target"), Status::kNotFound);
}

TEST_F(NamespaceManagerTest, Rename_DestinationExists_ReturnsAlreadyExists)
{
    Inode f1, f2;
    ns_mgr.CreateFile("/src.txt", 0644, 0, 0, 0, f1);
    ns_mgr.CreateFile("/dst.txt", 0644, 0, 0, 0, f2);

    EXPECT_EQ(ns_mgr.Rename("/src.txt", "/dst.txt"), Status::kAlreadyExists);
}

TEST_F(NamespaceManagerTest, Rename_UpdatesInodeMetadata)
{
    Inode file;
    ns_mgr.CreateFile("/original.txt", 0644, 0, 0, 0, file);

    ns_mgr.Rename("/original.txt", "/renamed.txt");

    Inode found;
    ns_mgr.Lookup("/renamed.txt", found);
    EXPECT_EQ(found.name, "renamed.txt");
    EXPECT_GT(found.ctime_ns, 0u);
}