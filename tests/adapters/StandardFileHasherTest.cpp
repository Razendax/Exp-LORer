#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "StandardFileHasher.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    class StandardFileHasherTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_file_hasher_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };
}

TEST_F(StandardFileHasherTest, IdenticalContentHashesIdenticallyRegardlessOfPathOrMtime)
{
    writeFile(m_tempDir / "a.txt", "hello world");
    writeFile(m_tempDir / "b.txt", "hello world");

    auto hashA = StandardFileHasher::hashFileContent(m_tempDir / "a.txt");
    auto hashB = StandardFileHasher::hashFileContent(m_tempDir / "b.txt");

    ASSERT_TRUE(hashA.hasValue());
    ASSERT_TRUE(hashB.hasValue());
    EXPECT_EQ(hashA.value(), hashB.value());
}

// Not a guarantee for xxHash64 (or any non-cryptographic hash), only the standard statistical
// expectation for a deliberately-chosen one-byte difference -- documented here, not asserted as a
// universal property.
TEST_F(StandardFileHasherTest, DifferingContentHashesDifferently)
{
    writeFile(m_tempDir / "a.txt", "hello world");
    writeFile(m_tempDir / "b.txt", "hello worlD"); // same size, last byte differs

    auto hashA = StandardFileHasher::hashFileContent(m_tempDir / "a.txt");
    auto hashB = StandardFileHasher::hashFileContent(m_tempDir / "b.txt");

    ASSERT_TRUE(hashA.hasValue());
    ASSERT_TRUE(hashB.hasValue());
    EXPECT_NE(hashA.value(), hashB.value());
}

TEST_F(StandardFileHasherTest, LargeContentSpanningMultipleChunksHashesIdentically)
{
    const std::string content(600 * 1024, 'x'); // larger than the hasher's 256KB streaming chunk
    writeFile(m_tempDir / "a.txt", content);
    writeFile(m_tempDir / "b.txt", content);

    auto hashA = StandardFileHasher::hashFileContent(m_tempDir / "a.txt");
    auto hashB = StandardFileHasher::hashFileContent(m_tempDir / "b.txt");

    ASSERT_TRUE(hashA.hasValue());
    ASSERT_TRUE(hashB.hasValue());
    EXPECT_EQ(hashA.value(), hashB.value());
}

TEST_F(StandardFileHasherTest, FailsForMissingFile)
{
    auto result = StandardFileHasher::hashFileContent(m_tempDir / "does-not-exist.txt");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}
