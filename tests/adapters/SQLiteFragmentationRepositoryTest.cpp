#include <algorithm>

#include <gtest/gtest.h>

#include "SQLiteFragmentationRepository.h"

namespace
{
    namespace fs = std::filesystem;

    class SQLiteFragmentationRepositoryTest : public ::testing::Test
    {
    protected:
        SQLiteFragmentationRepository m_repository{ fs::path(":memory:") };
    };

    bool containsPath(const std::vector<FileFragmentationInfo>& files, const fs::path& path)
    {
        return std::any_of(files.begin(), files.end(), [&path](const FileFragmentationInfo& file) { return file.path == path; });
    }
}

TEST_F(SQLiteFragmentationRepositoryTest, SaveScanThenFilesUnderReturnsFilesStrictlyUnderFolder)
{
    const fs::path root = "C:/repo";
    const std::vector<FileFragmentationInfo> files = {
        FileFragmentationInfo{ root / "a.txt", 100, 1 },
        FileFragmentationInfo{ root / "sub" / "b.txt", 200, 5 },
    };

    ASSERT_TRUE(m_repository.saveScan(root, 0, files).hasValue());

    auto rootFiles = m_repository.filesUnder(root);
    ASSERT_TRUE(rootFiles.hasValue());
    ASSERT_EQ(rootFiles.value().size(), 2u);
    EXPECT_TRUE(containsPath(rootFiles.value(), root / "a.txt"));
    EXPECT_TRUE(containsPath(rootFiles.value(), root / "sub" / "b.txt"));

    auto subFiles = m_repository.filesUnder(root / "sub");
    ASSERT_TRUE(subFiles.hasValue());
    ASSERT_EQ(subFiles.value().size(), 1u);
    EXPECT_EQ(subFiles.value()[0].path, root / "sub" / "b.txt");
    EXPECT_EQ(subFiles.value()[0].fragmentCount, 5u);
}

TEST_F(SQLiteFragmentationRepositoryTest, FilesUnderReturnsEmptyForNeverScannedFolder)
{
    auto result = m_repository.filesUnder("C:/never-scanned");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(SQLiteFragmentationRepositoryTest, RescanUpdatesExistingFileFragmentCount)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(m_repository.saveScan(root, 0, { FileFragmentationInfo{ root / "a.txt", 100, 3 } }).hasValue());
    ASSERT_TRUE(m_repository.saveScan(root, 0, { FileFragmentationInfo{ root / "a.txt", 100, 1 } }).hasValue());

    auto files = m_repository.filesUnder(root);
    ASSERT_TRUE(files.hasValue());
    ASSERT_EQ(files.value().size(), 1u);
    EXPECT_EQ(files.value()[0].fragmentCount, 1u);
}

TEST_F(SQLiteFragmentationRepositoryTest, RescanDeletesFileRemovedFromDisk)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(m_repository
                    .saveScan(root, 0,
                              { FileFragmentationInfo{ root / "keep.txt", 100, 1 }, FileFragmentationInfo{ root / "gone.txt", 200, 2 } })
                    .hasValue());

    // Re-scan without "gone.txt" at the same threshold -- it was removed from disk between scans.
    ASSERT_TRUE(m_repository.saveScan(root, 0, { FileFragmentationInfo{ root / "keep.txt", 100, 1 } }).hasValue());

    auto files = m_repository.filesUnder(root);
    ASSERT_TRUE(files.hasValue());
    ASSERT_EQ(files.value().size(), 1u);
    EXPECT_EQ(files.value()[0].path, root / "keep.txt");
}

TEST_F(SQLiteFragmentationRepositoryTest, RescanWithLargerMinSizeDoesNotDeletePreviouslySavedSmallerFileRows)
{
    const fs::path root = "C:/repo";
    // First scan at a 0-byte threshold covers both files.
    ASSERT_TRUE(m_repository
                    .saveScan(root, 0, { FileFragmentationInfo{ root / "big.txt", 1000, 4 }, FileFragmentationInfo{ root / "small.txt", 10, 1 } })
                    .hasValue());

    // Second scan at a 500-byte threshold never examines "small.txt" at all (it's below this
    // scan's own candidate set) -- it must survive untouched, not be deleted as "absent from this
    // scan". "big.txt" is this scan's only candidate and is re-saved (fragment count updated).
    ASSERT_TRUE(m_repository.saveScan(root, 500, { FileFragmentationInfo{ root / "big.txt", 1000, 7 } }).hasValue());

    auto files = m_repository.filesUnder(root);
    ASSERT_TRUE(files.hasValue());
    EXPECT_TRUE(containsPath(files.value(), root / "small.txt"));
    ASSERT_TRUE(containsPath(files.value(), root / "big.txt"));
    const auto bigFile = std::find_if(files.value().begin(), files.value().end(),
                                       [&root](const FileFragmentationInfo& file) { return file.path == root / "big.txt"; });
    ASSERT_NE(bigFile, files.value().end());
    EXPECT_EQ(bigFile->fragmentCount, 7u);
}

TEST_F(SQLiteFragmentationRepositoryTest, FilesUnderDoesNotReturnSiblingsOutsideFolder)
{
    const fs::path root = "C:/repo";
    ASSERT_TRUE(
        m_repository
            .saveScan(root, 0, { FileFragmentationInfo{ root / "a" / "x.txt", 100, 1 }, FileFragmentationInfo{ root / "b" / "y.txt", 200, 2 } })
            .hasValue());

    auto aFiles = m_repository.filesUnder(root / "a");
    ASSERT_TRUE(aFiles.hasValue());
    ASSERT_EQ(aFiles.value().size(), 1u);
    EXPECT_EQ(aFiles.value()[0].path, root / "a" / "x.txt");
}
