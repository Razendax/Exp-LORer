#include <gtest/gtest.h>

#include <fstream>

#include "StandardFileSystemRepository.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    class StandardFileSystemRepositoryTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_fs_repo_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        StandardFileSystemRepository m_repository;
    };
}

TEST_F(StandardFileSystemRepositoryTest, ListDirectoryReturnsEntries)
{
    writeFile(m_tempDir / "a.txt", "hello");
    fs::create_directory(m_tempDir / "sub");

    auto result = m_repository.listDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u);
}

TEST_F(StandardFileSystemRepositoryTest, ListDirectoryFailsForMissingPath)
{
    auto result = m_repository.listDirectory(m_tempDir / "does-not-exist");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(StandardFileSystemRepositoryTest, StatResolvesDirectory)
{
    auto result = m_repository.stat(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isDirectory());
}

TEST_F(StandardFileSystemRepositoryTest, StatResolvesFile)
{
    writeFile(m_tempDir / "a.txt", "hello");

    auto result = m_repository.stat(m_tempDir / "a.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().isDirectory());
    EXPECT_EQ(result.value().size(), 5u);
}

TEST_F(StandardFileSystemRepositoryTest, StatFailsForMissingPath)
{
    auto result = m_repository.stat(m_tempDir / "does-not-exist");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(StandardFileSystemRepositoryTest, MoveRenamesFile)
{
    writeFile(m_tempDir / "source.txt", "data");

    auto result = m_repository.move(m_tempDir / "source.txt", m_tempDir / "dest.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::exists(m_tempDir / "dest.txt"));
    EXPECT_FALSE(fs::exists(m_tempDir / "source.txt"));
}

TEST_F(StandardFileSystemRepositoryTest, CopyDuplicatesFile)
{
    writeFile(m_tempDir / "source.txt", "data");

    auto result = m_repository.copy(m_tempDir / "source.txt", m_tempDir / "copy.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::exists(m_tempDir / "source.txt"));
    EXPECT_TRUE(fs::exists(m_tempDir / "copy.txt"));
}

TEST_F(StandardFileSystemRepositoryTest, DeletePermanentlyRemovesFile)
{
    writeFile(m_tempDir / "gone.txt", "data");

    auto result = m_repository.deletePermanently(m_tempDir / "gone.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(fs::exists(m_tempDir / "gone.txt"));
}

TEST_F(StandardFileSystemRepositoryTest, ComputeFileHashIsDeterministic)
{
    writeFile(m_tempDir / "hash.txt", "some content for hashing");
    auto listing = m_repository.listDirectory(m_tempDir);
    ASSERT_TRUE(listing.hasValue());
    ASSERT_FALSE(listing.value().empty());

    const FileNode& file = listing.value().front();

    auto first = m_repository.computeFileHash(file);
    auto second = m_repository.computeFileHash(file);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value(), second.value());
}
