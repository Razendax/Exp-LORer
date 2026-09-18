#include <gtest/gtest.h>

#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "StandardFileSystemRepository.h"
#include "VirtualPaths.h"

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

TEST_F(StandardFileSystemRepositoryTest, ListDirectoryRecursiveReturnsAllDescendants)
{
    writeFile(m_tempDir / "a.txt", "hello");
    fs::create_directory(m_tempDir / "sub");
    writeFile(m_tempDir / "sub" / "b.txt", "world");
    fs::create_directory(m_tempDir / "sub" / "nested");
    writeFile(m_tempDir / "sub" / "nested" / "c.txt", "!");

    auto result = m_repository.listDirectoryRecursive(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    // a.txt, sub, sub/b.txt, sub/nested, sub/nested/c.txt
    EXPECT_EQ(result.value().size(), 5u);
}

TEST_F(StandardFileSystemRepositoryTest, ListDirectoryRecursiveRejectsThisPc)
{
    auto result = m_repository.listDirectoryRecursive(VirtualPaths::ThisPC);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST_F(StandardFileSystemRepositoryTest, ListDirectoryRecursiveFailsForMissingPath)
{
    auto result = m_repository.listDirectoryRecursive(m_tempDir / "does-not-exist");

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

TEST_F(StandardFileSystemRepositoryTest, StatResolvesThisPcAsSyntheticDirectory)
{
    auto result = m_repository.stat(VirtualPaths::ThisPC);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isDirectory());
    ASSERT_TRUE(result.value().displayName().has_value());
    EXPECT_EQ(result.value().displayName().value(), "This PC");
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

TEST_F(StandardFileSystemRepositoryTest, CreateDirectoryCreatesNewFolder)
{
    auto result = m_repository.createDirectory(m_tempDir / "New folder");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::is_directory(m_tempDir / "New folder"));
}

TEST_F(StandardFileSystemRepositoryTest, CreateDirectoryFailsIfAlreadyExists)
{
    fs::create_directory(m_tempDir / "existing");

    auto result = m_repository.createDirectory(m_tempDir / "existing");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

TEST_F(StandardFileSystemRepositoryTest, CreateFileFromTemplateCreatesEmptyFileWhenNoTemplateGiven)
{
    auto result = m_repository.createFileFromTemplate(m_tempDir / "New Text Document.txt", std::nullopt);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::exists(m_tempDir / "New Text Document.txt"));
    EXPECT_EQ(fs::file_size(m_tempDir / "New Text Document.txt"), 0u);
}

TEST_F(StandardFileSystemRepositoryTest, CreateFileFromTemplateCopiesTemplateBytes)
{
    writeFile(m_tempDir / "template.txt", "template content");

    auto result = m_repository.createFileFromTemplate(m_tempDir / "New file.txt", m_tempDir / "template.txt");

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(fs::exists(m_tempDir / "New file.txt"));
    EXPECT_EQ(fs::file_size(m_tempDir / "New file.txt"), fs::file_size(m_tempDir / "template.txt"));
}

TEST_F(StandardFileSystemRepositoryTest, CreateFileFromTemplateFailsIfDestinationAlreadyExists)
{
    writeFile(m_tempDir / "already-there.txt", "data");

    auto result = m_repository.createFileFromTemplate(m_tempDir / "already-there.txt", std::nullopt);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::AlreadyExists);
}

#ifdef _WIN32
TEST_F(StandardFileSystemRepositoryTest, ListDirectoryReportsWindowsHiddenAttribute)
{
    writeFile(m_tempDir / "visible.txt", "hello");
    writeFile(m_tempDir / "hidden.txt", "secret");
    ASSERT_TRUE(SetFileAttributesW((m_tempDir / "hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN));

    auto result = m_repository.listDirectory(m_tempDir);

    ASSERT_TRUE(result.hasValue());
    bool foundVisible = false;
    bool foundHidden = false;
    for (const FileNode& entry : result.value())
    {
        if (entry.name() == "visible.txt")
        {
            foundVisible = true;
            EXPECT_FALSE(entry.isHidden());
        }
        else if (entry.name() == "hidden.txt")
        {
            foundHidden = true;
            EXPECT_TRUE(entry.isHidden());
        }
    }
    EXPECT_TRUE(foundVisible);
    EXPECT_TRUE(foundHidden);
}

TEST_F(StandardFileSystemRepositoryTest, StatReportsWindowsHiddenAttribute)
{
    writeFile(m_tempDir / "hidden.txt", "secret");
    ASSERT_TRUE(SetFileAttributesW((m_tempDir / "hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN));

    auto result = m_repository.stat(m_tempDir / "hidden.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isHidden());
}
#endif

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
