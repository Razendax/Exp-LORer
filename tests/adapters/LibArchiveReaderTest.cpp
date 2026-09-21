#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>

#include "ArchiveFixture.h"
#include "LibArchiveReader.h"

namespace
{
    namespace fs = std::filesystem;

    class LibArchiveReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_libarchive_reader_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        std::string readFile(const fs::path& path) const
        {
            std::ifstream stream(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        }

        fs::path m_tempDir;
    };
}

TEST_F(LibArchiveReaderTest, ListEntriesReturnsFlatList)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, {
                                               { "vacation/img1.jpg", "jpgbytes" },
                                               { "vacation/img2.jpg", "jpgbytes2" },
                                               { "readme.txt", "hello" },
                                           });

    auto result = LibArchiveReader::listEntries(archivePath);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 3u);

    const auto& entries = result.value();
    EXPECT_TRUE(std::any_of(entries.begin(), entries.end(),
                             [](const auto& e) { return e.relativePath == fs::path("readme.txt") && e.size == 5u; }));
    EXPECT_TRUE(std::any_of(entries.begin(), entries.end(),
                             [](const auto& e) { return e.relativePath == fs::path("vacation") / "img1.jpg"; }));
}

TEST_F(LibArchiveReaderTest, ListEntriesFailsForNonArchiveFile)
{
    const fs::path notAnArchive = m_tempDir / "notes.txt";
    std::ofstream(notAnArchive, std::ios::binary) << "plain text content, not any recognized archive format at all";

    auto result = LibArchiveReader::listEntries(notAnArchive);

    EXPECT_TRUE(result.hasError());
}

TEST_F(LibArchiveReaderTest, ExtractEntriesWritesWholeArchiveWhenPrefixEmpty)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, {
                                               { "vacation/img.jpg", "jpgbytes" },
                                               { "readme.txt", "hello" },
                                           });

    const fs::path destination = m_tempDir / "out";
    auto result = LibArchiveReader::extractEntries(archivePath, {}, destination);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(fs::exists(destination / "vacation" / "img.jpg"));
    ASSERT_TRUE(fs::exists(destination / "readme.txt"));
    EXPECT_EQ(readFile(destination / "readme.txt"), "hello");
}

TEST_F(LibArchiveReaderTest, ExtractEntriesRestructuresRelativeToFolderPrefix)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, {
                                               { "vacation/img.jpg", "jpgbytes" },
                                               { "readme.txt", "hello" },
                                           });

    const fs::path destination = m_tempDir / "vacationCopy";
    auto result = LibArchiveReader::extractEntries(archivePath, "vacation", destination);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::exists(destination / "img.jpg"));
    EXPECT_FALSE(fs::exists(destination / "readme.txt"));
}

TEST_F(LibArchiveReaderTest, ExtractEntriesWritesSingleFilePrefixByItsFilename)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "docs/readme.txt", "hello world" } });

    const fs::path destination = m_tempDir / "temp-open";
    auto result = LibArchiveReader::extractEntries(archivePath, fs::path("docs") / "readme.txt", destination);

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(fs::exists(destination / "readme.txt"));
    EXPECT_EQ(readFile(destination / "readme.txt"), "hello world");
}

TEST_F(LibArchiveReaderTest, ExtractEntriesFailsWhenPrefixMatchesNoEntry)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" } });

    const fs::path destination = m_tempDir / "out";
    auto result = LibArchiveReader::extractEntries(archivePath, "does-not-exist.txt", destination);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// Zip Slip (path traversal): a malicious/corrupted archive can claim any entry name it likes --
// extractEntries must never let a ".." or absolute entry path escape the destination directory.
TEST_F(LibArchiveReaderTest, ExtractEntriesRejectsPathTraversalEntry)
{
    const fs::path archivePath = m_tempDir / "malicious.zip";
    ArchiveFixture::writeZip(archivePath, {
                                               { "../../evil.txt", "payload" },
                                               { "readme.txt", "hello" },
                                           });

    const fs::path destination = m_tempDir / "out";
    auto result = LibArchiveReader::extractEntries(archivePath, {}, destination);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(fs::exists(destination / "readme.txt"));
    // The traversal entry must land nowhere on disk -- neither escaping to m_tempDir's parent nor
    // anywhere inside destination.
    EXPECT_FALSE(fs::exists(m_tempDir.parent_path() / "evil.txt"));
    EXPECT_FALSE(fs::exists(destination / "evil.txt"));
    EXPECT_FALSE(fs::exists(destination / ".." / ".." / "evil.txt"));
}

TEST_F(LibArchiveReaderTest, ExtractEntriesRejectsAbsolutePathEntry)
{
    const fs::path archivePath = m_tempDir / "malicious-absolute.zip";
    // A rooted entry path (leading '/'), independent of any real on-disk path -- has_root_directory()
    // alone must be enough to reject it, since fs::path::is_absolute() on Windows requires a drive
    // letter too and would otherwise miss this.
    ArchiveFixture::writeZip(archivePath, { { "/outside.txt", "payload" } });

    const fs::path destination = m_tempDir / "out2";
    auto result = LibArchiveReader::extractEntries(archivePath, {}, destination);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(fs::exists(destination / "outside.txt"));
    EXPECT_FALSE(fs::exists(m_tempDir.root_path() / "outside.txt"));
}

TEST_F(LibArchiveReaderTest, ListEntriesOmitsPathTraversalEntries)
{
    const fs::path archivePath = m_tempDir / "malicious-list.zip";
    ArchiveFixture::writeZip(archivePath, {
                                               { "../escape.txt", "payload" },
                                               { "readme.txt", "hello" },
                                           });

    auto result = LibArchiveReader::listEntries(archivePath);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value().front().relativePath, fs::path("readme.txt"));
}
