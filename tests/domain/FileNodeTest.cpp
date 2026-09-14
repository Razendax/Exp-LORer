#include <gtest/gtest.h>

#include "FileNode.h"

namespace
{
    std::chrono::system_clock::time_point someTime()
    {
        return std::chrono::system_clock::from_time_t(1'700'000'000);
    }
}

TEST(FileNode, CreateSucceedsWithValidPath)
{
    auto result = FileNode::create("C:/data/report.pdf", 1024, someTime(), someTime(), FileType::Regular);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().path(), "C:/data/report.pdf");
    EXPECT_EQ(result.value().name(), "report.pdf");
    EXPECT_EQ(result.value().size(), 1024u);
    EXPECT_EQ(result.value().fileType(), FileType::Regular);
    EXPECT_FALSE(result.value().hash().has_value());
    EXPECT_FALSE(result.value().isDirectory());
}

TEST(FileNode, CreateFailsWithEmptyPath)
{
    auto result = FileNode::create("", 0, someTime(), someTime(), FileType::Regular);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(FileNode, IsDirectoryReflectsFileType)
{
    auto result = FileNode::create("C:/data", 0, someTime(), someTime(), FileType::Directory);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isDirectory());
}

TEST(FileNode, WithHashReturnsCopyCarryingHash)
{
    auto original = FileNode::create("C:/data/video.mp4", 2048, someTime(), someTime(), FileType::Regular).value();

    FileNode withHash = original.withHash(0xDEADBEEF);

    EXPECT_FALSE(original.hash().has_value());
    ASSERT_TRUE(withHash.hash().has_value());
    EXPECT_EQ(withHash.hash().value(), 0xDEADBEEFu);
    EXPECT_NE(original, withHash);
}

TEST(FileNode, EqualityComparesAllFields)
{
    auto a = FileNode::create("C:/data/a.txt", 10, someTime(), someTime(), FileType::Regular).value();
    auto b = FileNode::create("C:/data/a.txt", 10, someTime(), someTime(), FileType::Regular).value();
    auto c = FileNode::create("C:/data/a.txt", 20, someTime(), someTime(), FileType::Regular).value();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
