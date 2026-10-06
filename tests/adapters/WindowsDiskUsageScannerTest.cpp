#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <optional>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "WindowsDiskUsageScanner.h"

namespace
{
    namespace fs = std::filesystem;

    std::atomic<bool> kNotCancelled{ false };

    void writeFile(const fs::path& path, const std::string& content)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << content;
    }

    std::optional<DiskUsageNode> findByPath(const std::vector<DiskUsageNode>& nodes, const fs::path& path)
    {
        for (const auto& node : nodes)
        {
            if (node.path == path)
            {
                return node;
            }
        }
        return std::nullopt;
    }

    class WindowsDiskUsageScannerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_disk_usage_scanner_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        WindowsDiskUsageScanner m_scanner;
    };
}

TEST_F(WindowsDiskUsageScannerTest, ScanAggregatesNestedDirectorySizes)
{
    writeFile(m_tempDir / "top.txt", std::string(10, 'a')); // 10 bytes at the root
    fs::create_directory(m_tempDir / "sub");
    writeFile(m_tempDir / "sub" / "a.txt", std::string(20, 'b'));
    writeFile(m_tempDir / "sub" / "b.txt", std::string(30, 'c'));
    fs::create_directory(m_tempDir / "sub" / "nested");
    writeFile(m_tempDir / "sub" / "nested" / "c.txt", std::string(40, 'd'));

    auto result = m_scanner.scan(m_tempDir, nullptr, kNotCancelled);
    ASSERT_TRUE(result.hasValue());

    const auto& nodes = result.value();

    auto root = findByPath(nodes, m_tempDir);
    ASSERT_TRUE(root.has_value());
    EXPECT_TRUE(root->isDirectory);
    EXPECT_EQ(root->size, 100u); // 10 + 20 + 30 + 40

    auto sub = findByPath(nodes, m_tempDir / "sub");
    ASSERT_TRUE(sub.has_value());
    EXPECT_TRUE(sub->isDirectory);
    EXPECT_EQ(sub->size, 90u); // 20 + 30 + 40

    auto nested = findByPath(nodes, m_tempDir / "sub" / "nested");
    ASSERT_TRUE(nested.has_value());
    EXPECT_EQ(nested->size, 40u);

    auto topFile = findByPath(nodes, m_tempDir / "top.txt");
    ASSERT_TRUE(topFile.has_value());
    EXPECT_FALSE(topFile->isDirectory);
    EXPECT_EQ(topFile->size, 10u);
}

TEST_F(WindowsDiskUsageScannerTest, ScanFailsForMissingPath)
{
    auto result = m_scanner.scan(m_tempDir / "does-not-exist", nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(WindowsDiskUsageScannerTest, ScanHandlesNonAsciiNamesWithoutThrowing)
{
    const fs::path nonAsciiDir = m_tempDir / fs::path(std::u8string(u8"Café"));
    fs::create_directory(nonAsciiDir);
    writeFile(nonAsciiDir / fs::path(std::u8string(u8"文件.txt")), std::string(5, 'x'));

    Result<std::vector<DiskUsageNode>> result = Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::IoError, "unset"));
    EXPECT_NO_THROW(result = m_scanner.scan(m_tempDir, nullptr, kNotCancelled));

    ASSERT_TRUE(result.hasValue());
    auto dir = findByPath(result.value(), nonAsciiDir);
    ASSERT_TRUE(dir.has_value());
    EXPECT_EQ(dir->size, 5u);
}

TEST_F(WindowsDiskUsageScannerTest, ScanRespectsCancellation)
{
    writeFile(m_tempDir / "a.txt", std::string(10, 'a'));

    std::atomic<bool> cancelled{ true };
    auto result = m_scanner.scan(m_tempDir, nullptr, cancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

#ifdef _WIN32
TEST_F(WindowsDiskUsageScannerTest, DoesNotRecurseIntoReparsePointDirectories)
{
    fs::create_directory(m_tempDir / "target");
    writeFile(m_tempDir / "target" / "hidden.txt", std::string(500, 'x'));

    const fs::path linkPath = m_tempDir / "link";
    const DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (!CreateSymbolicLinkW(linkPath.c_str(), (m_tempDir / "target").c_str(), flags))
    {
        GTEST_SKIP() << "Symlink creation not permitted in this environment";
    }

    auto result = m_scanner.scan(m_tempDir, nullptr, kNotCancelled);
    ASSERT_TRUE(result.hasValue());

    // "target/hidden.txt" is found normally; a *second* copy reached only by recursing through
    // "link" (i.e. at "link/hidden.txt") would mean the reparse point wasn't skipped.
    EXPECT_TRUE(findByPath(result.value(), m_tempDir / "target" / "hidden.txt").has_value());
    EXPECT_FALSE(findByPath(result.value(), linkPath / "hidden.txt").has_value());

    auto link = findByPath(result.value(), linkPath);
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->size, 0u);
}
#endif
