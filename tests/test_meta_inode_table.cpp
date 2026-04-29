#include <gtest/gtest.h>
#include "meta/inode_table.h"

using namespace openfs;

class InodeTableTest : public ::testing::Test
{
protected:
    InodeTable table;
};

// ============================================================
// Root inode
// ============================================================
TEST_F(InodeTableTest, RootInode_ExistsAtId1)
{
    Inode inode;
    EXPECT_EQ(table.Get(1, inode), Status::kOk);
    EXPECT_EQ(inode.inode_id, 1u);
    EXPECT_EQ(inode.file_type, InodeType::kDirectory);
    EXPECT_EQ(inode.name, "/");
}

TEST_F(InodeTableTest, RootInode_HasNlink2)
{
    Inode inode;
    table.Get(1, inode);
    EXPECT_EQ(inode.nlink, 2u);
}

// ============================================================
// Create
// ============================================================
TEST_F(InodeTableTest, Create_Success)
{
    Inode inode;
    inode.inode_id = 2;
    inode.file_type = InodeType::kRegular;
    inode.name = "test.txt";

    EXPECT_EQ(table.Create(inode), Status::kOk);
    EXPECT_TRUE(table.Exists(2));
}

TEST_F(InodeTableTest, Create_DuplicateId_ReturnsAlreadyExists)
{
    Inode inode;
    inode.inode_id = 1; // Root already exists
    EXPECT_EQ(table.Create(inode), Status::kAlreadyExists);
}

TEST_F(InodeTableTest, Create_MultipleInodes)
{
    for (int i = 0; i < 10; ++i)
    {
        Inode inode;
        inode.inode_id = table.AllocateInodeId();
        inode.name = "file_" + std::to_string(i);
        EXPECT_EQ(table.Create(inode), Status::kOk);
    }

    Inode check;
    for (uint64_t id = 2; id <= 11; ++id)
    {
        EXPECT_EQ(table.Get(id, check), Status::kOk);
        EXPECT_EQ(check.inode_id, id);
    }
}

// ============================================================
// Get
// ============================================================
TEST_F(InodeTableTest, Get_Nonexistent_ReturnsNotFound)
{
    Inode inode;
    EXPECT_EQ(table.Get(999, inode), Status::kNotFound);
}

TEST_F(InodeTableTest, Get_ReturnsCorrectData)
{
    Inode inode;
    inode.inode_id = 5;
    inode.file_type = InodeType::kRegular;
    inode.size = 1024;
    inode.mode = 0755;
    inode.name = "data.bin";
    table.Create(inode);

    Inode retrieved;
    EXPECT_EQ(table.Get(5, retrieved), Status::kOk);
    EXPECT_EQ(retrieved.size, 1024u);
    EXPECT_EQ(retrieved.mode, 0755u);
    EXPECT_EQ(retrieved.name, "data.bin");
}

// ============================================================
// Update
// ============================================================
TEST_F(InodeTableTest, Update_Success)
{
    Inode inode;
    inode.inode_id = 2;
    inode.size = 100;
    inode.name = "original";
    table.Create(inode);

    inode.size = 200;
    inode.name = "updated";
    EXPECT_EQ(table.Update(inode), Status::kOk);

    Inode retrieved;
    table.Get(2, retrieved);
    EXPECT_EQ(retrieved.size, 200u);
    EXPECT_EQ(retrieved.name, "updated");
}

TEST_F(InodeTableTest, Update_Nonexistent_ReturnsNotFound)
{
    Inode inode;
    inode.inode_id = 999;
    EXPECT_EQ(table.Update(inode), Status::kNotFound);
}

// ============================================================
// Delete
// ============================================================
TEST_F(InodeTableTest, Delete_Success)
{
    Inode inode;
    inode.inode_id = 2;
    inode.name = "to_delete";
    table.Create(inode);

    EXPECT_EQ(table.Delete(2), Status::kOk);
    EXPECT_FALSE(table.Exists(2));
}

TEST_F(InodeTableTest, Delete_Nonexistent_ReturnsNotFound)
{
    EXPECT_EQ(table.Delete(999), Status::kNotFound);
}

// ============================================================
// AllocateInodeId
// ============================================================
TEST_F(InodeTableTest, AllocateInodeId_StartsFrom2)
{
    uint64_t id = table.AllocateInodeId();
    EXPECT_EQ(id, 2u);
}

TEST_F(InodeTableTest, AllocateInodeId_Increments)
{
    uint64_t id1 = table.AllocateInodeId();
    uint64_t id2 = table.AllocateInodeId();
    EXPECT_GT(id2, id1);
}

// ============================================================
// Exists
// ============================================================
TEST_F(InodeTableTest, Exists_RootInode)
{
    EXPECT_TRUE(table.Exists(1));
}

TEST_F(InodeTableTest, Exists_Nonexistent)
{
    EXPECT_FALSE(table.Exists(999));
}