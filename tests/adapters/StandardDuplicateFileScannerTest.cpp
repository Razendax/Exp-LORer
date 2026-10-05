#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <string>

#include "StandardDuplicateFileScanner.h"
#include "StandardFileSystemRepository.h"

namespace
{
    namespace fs = std::filesystem;

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    bool groupContainsExactly(const std::vector<DuplicateFileGroup>& groups, const std::vector<fs::path>& expectedPaths)
    {
        for (const auto& group : groups)
        {
            if (group.paths.size() != expectedPaths.size())
            {
                continue;
            }

            const bool allFound = std::all_of(expectedPaths.begin(), expectedPaths.end(), [&group](const fs::path& expected) {
                return std::find(group.paths.begin(), group.paths.end(), expected) != group.paths.end();
            });
            if (allFound)
            {
                return true;
            }
        }
        return false;
    }

    class StandardDuplicateFileScannerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_duplicate_scanner_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        StandardFileSystemRepository m_fileSystemRepository;
        StandardDuplicateFileScanner m_scanner{ m_fileSystemRepository };
        std::atomic<bool> m_cancelFlag{ false };
    };
}

TEST_F(StandardDuplicateFileScannerTest, GroupsIdenticalContentAcrossSubfolders)
{
    fs::create_directory(m_tempDir / "sub1");
    fs::create_directory(m_tempDir / "sub2");
    writeFile(m_tempDir / "sub1" / "a.txt", "duplicate content");
    writeFile(m_tempDir / "sub2" / "b.txt", "duplicate content");
    writeFile(m_tempDir / "unique.txt", "something else entirely, different size");

    auto result = m_scanner.scan(m_tempDir, 0, nullptr, m_cancelFlag);
    ASSERT_TRUE(result.hasValue());

    EXPECT_TRUE(groupContainsExactly(result.value(), { m_tempDir / "sub1" / "a.txt", m_tempDir / "sub2" / "b.txt" }));

    for (const auto& group : result.value())
    {
        for (const auto& path : group.paths)
        {
            EXPECT_NE(path, m_tempDir / "unique.txt");
        }
    }
}

TEST_F(StandardDuplicateFileScannerTest, SameSizeDifferentContentFilesAreNotGrouped)
{
    writeFile(m_tempDir / "a.txt", "aaaaaaaaaa"); // 10 bytes
    writeFile(m_tempDir / "b.txt", "bbbbbbbbbb"); // 10 bytes, same size, different content

    auto result = m_scanner.scan(m_tempDir, 0, nullptr, m_cancelFlag);
    ASSERT_TRUE(result.hasValue());

    EXPECT_TRUE(result.value().empty());
}

TEST_F(StandardDuplicateFileScannerTest, ExcludesFilesBelowMinimumSize)
{
    writeFile(m_tempDir / "small1.txt", "hi");
    writeFile(m_tempDir / "small2.txt", "hi");

    auto result = m_scanner.scan(m_tempDir, 100, nullptr, m_cancelFlag);
    ASSERT_TRUE(result.hasValue());

    EXPECT_TRUE(result.value().empty());
}

TEST_F(StandardDuplicateFileScannerTest, CancellationReturnsErrorWithoutPartialGroups)
{
    writeFile(m_tempDir / "a.txt", "duplicate content");
    writeFile(m_tempDir / "b.txt", "duplicate content");

    m_cancelFlag.store(true);

    auto result = m_scanner.scan(m_tempDir, 0, nullptr, m_cancelFlag);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}
