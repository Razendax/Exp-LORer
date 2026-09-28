#include <algorithm>

#include <gtest/gtest.h>

#include "SQLiteDiskUsageRepository.h"

namespace
{
    namespace fs = std::filesystem;

    class SQLiteDiskUsageRepositoryTest : public ::testing::Test
    {
    protected:
        SQLiteDiskUsageRepository m_repository{ fs::path(":memory:") };
    };

    bool containsName(const std::vector<DiskUsageNode>& nodes, const std::string& name)
    {
        return std::any_of(nodes.begin(), nodes.end(), [&name](const DiskUsageNode& node) { return node.name == name; });
    }
}

TEST_F(SQLiteDiskUsageRepositoryTest, SaveScanThenChildrenOfReturnsImmediateChildren)
{
    const fs::path root = "C:/repo";
    const std::vector<DiskUsageNode> nodes = {
        DiskUsageNode{ root, "repo", 300, true },
        DiskUsageNode{ root / "sub", "sub", 200, true },
        DiskUsageNode{ root / "file.txt", "file.txt", 100, false },
        DiskUsageNode{ root / "sub" / "nested.txt", "nested.txt", 200, false },
    };

    ASSERT_TRUE(m_repository.saveScan(root, nodes).hasValue());

    auto rootChildren = m_repository.childrenOf(root);
    ASSERT_TRUE(rootChildren.hasValue());
    ASSERT_EQ(rootChildren.value().size(), 2u);
    EXPECT_TRUE(containsName(rootChildren.value(), "sub"));
    EXPECT_TRUE(containsName(rootChildren.value(), "file.txt"));

    auto subChildren = m_repository.childrenOf(root / "sub");
    ASSERT_TRUE(subChildren.hasValue());
    ASSERT_EQ(subChildren.value().size(), 1u);
    EXPECT_EQ(subChildren.value()[0].name, "nested.txt");
    EXPECT_EQ(subChildren.value()[0].size, 200u);
}

TEST_F(SQLiteDiskUsageRepositoryTest, ChildrenOfReturnsEmptyForUnscannedFolder)
{
    auto result = m_repository.childrenOf("C:/never-scanned");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(SQLiteDiskUsageRepositoryTest, RescanUpdatesExistingRowSize)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(m_repository
                    .saveScan(root, { DiskUsageNode{ root, "repo", 100, true },
                                       DiskUsageNode{ root / "file.txt", "file.txt", 100, false } })
                    .hasValue());

    ASSERT_TRUE(m_repository
                    .saveScan(root, { DiskUsageNode{ root, "repo", 500, true },
                                       DiskUsageNode{ root / "file.txt", "file.txt", 500, false } })
                    .hasValue());

    auto children = m_repository.childrenOf(root);
    ASSERT_TRUE(children.hasValue());
    ASSERT_EQ(children.value().size(), 1u);
    EXPECT_EQ(children.value()[0].size, 500u);
}

TEST_F(SQLiteDiskUsageRepositoryTest, RescanDeletesRowsRemovedFromDisk)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(m_repository
                    .saveScan(root, { DiskUsageNode{ root, "repo", 300, true },
                                       DiskUsageNode{ root / "keep.txt", "keep.txt", 100, false },
                                       DiskUsageNode{ root / "gone.txt", "gone.txt", 200, false } })
                    .hasValue());

    // Re-scan without "gone.txt" -- it was deleted from disk between scans.
    ASSERT_TRUE(m_repository
                    .saveScan(root, { DiskUsageNode{ root, "repo", 100, true },
                                       DiskUsageNode{ root / "keep.txt", "keep.txt", 100, false } })
                    .hasValue());

    auto children = m_repository.childrenOf(root);
    ASSERT_TRUE(children.hasValue());
    ASSERT_EQ(children.value().size(), 1u);
    EXPECT_EQ(children.value()[0].name, "keep.txt");
}

TEST_F(SQLiteDiskUsageRepositoryTest, RescanOfSubsetDoesNotDeleteRowsOutsideScanRoot)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(m_repository
                    .saveScan(root, { DiskUsageNode{ root, "repo", 300, true },
                                       DiskUsageNode{ root / "a", "a", 100, true },
                                       DiskUsageNode{ root / "a" / "x.txt", "x.txt", 100, false },
                                       DiskUsageNode{ root / "b", "b", 200, true },
                                       DiskUsageNode{ root / "b" / "y.txt", "y.txt", 200, false } })
                    .hasValue());

    // Re-scanning only "a" must not touch "b"'s rows even though they share a parent scanned
    // earlier.
    ASSERT_TRUE(m_repository
                    .saveScan(root / "a", { DiskUsageNode{ root / "a", "a", 100, true },
                                             DiskUsageNode{ root / "a" / "x.txt", "x.txt", 100, false } })
                    .hasValue());

    auto rootChildren = m_repository.childrenOf(root);
    ASSERT_TRUE(rootChildren.hasValue());
    EXPECT_TRUE(containsName(rootChildren.value(), "a"));
    EXPECT_TRUE(containsName(rootChildren.value(), "b"));

    auto bChildren = m_repository.childrenOf(root / "b");
    ASSERT_TRUE(bChildren.hasValue());
    ASSERT_EQ(bChildren.value().size(), 1u);
    EXPECT_EQ(bChildren.value()[0].name, "y.txt");
}
