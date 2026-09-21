#include <gtest/gtest.h>

#include <fstream>

#include "ArchivePathResolver.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    class ArchivePathResolverTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_archive_path_resolver_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };
}

TEST_F(ArchivePathResolverTest, ReturnsNulloptForRealDirectory)
{
    EXPECT_FALSE(ArchivePathResolver::resolve(m_tempDir).has_value());
}

TEST_F(ArchivePathResolverTest, ReturnsNulloptForRealFileWithoutArchiveExtension)
{
    writeFile(m_tempDir / "notes.txt", "hello");

    EXPECT_FALSE(ArchivePathResolver::resolve(m_tempDir / "notes.txt").has_value());
}

TEST_F(ArchivePathResolverTest, ResolvesArchiveRootWithEmptyEntryPath)
{
    writeFile(m_tempDir / "photos.zip", "not real zip bytes");

    auto resolution = ArchivePathResolver::resolve(m_tempDir / "photos.zip");

    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->archiveFile, m_tempDir / "photos.zip");
    EXPECT_TRUE(resolution->entryPathInArchive.empty());
}

TEST_F(ArchivePathResolverTest, ResolvesEntryPathInsideArchive)
{
    writeFile(m_tempDir / "photos.zip", "not real zip bytes");

    auto resolution = ArchivePathResolver::resolve(m_tempDir / "photos.zip" / "vacation" / "img.jpg");

    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->archiveFile, m_tempDir / "photos.zip");
    EXPECT_EQ(resolution->entryPathInArchive, fs::path("vacation") / "img.jpg");
}

TEST_F(ArchivePathResolverTest, IgnoresRealFolderNamedLikeAnArchive)
{
    fs::create_directory(m_tempDir / "foo.zip");

    EXPECT_FALSE(ArchivePathResolver::resolve(m_tempDir / "foo.zip").has_value());
}

// Nested archives are not resolved further (Architecture.md §14.28) -- only a real on-disk file
// can ever match, so "other.zip" here is just an ordinary path component of the outer archive.
TEST_F(ArchivePathResolverTest, DoesNotResolveNestedArchiveFurther)
{
    writeFile(m_tempDir / "photos.zip", "not real zip bytes");

    auto resolution = ArchivePathResolver::resolve(m_tempDir / "photos.zip" / "other.zip" / "file.txt");

    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->archiveFile, m_tempDir / "photos.zip");
    EXPECT_EQ(resolution->entryPathInArchive, fs::path("other.zip") / "file.txt");
}
