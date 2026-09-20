#include <gtest/gtest.h>

#include "FilePreview.h"

namespace
{
    std::chrono::system_clock::time_point someTime()
    {
        return std::chrono::system_clock::from_time_t(1'700'000'000);
    }
}

TEST(FilePreview, FolderFactorySetsKindAndEntries)
{
    std::vector<FileNode> entries;
    entries.push_back(FileNode::create("C:/data/a.txt", 10, someTime(), someTime(), FileType::Regular).value());

    FilePreview preview = FilePreview::folder(entries);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Folder);
    ASSERT_EQ(preview.folderEntries().size(), 1u);
    EXPECT_EQ(preview.folderEntries().front().path(), "C:/data/a.txt");
    EXPECT_TRUE(preview.text().empty());
    EXPECT_TRUE(preview.imageBytes().empty());
}

TEST(FilePreview, TextFactorySetsKindContentTruncatedAndPath)
{
    FilePreview preview = FilePreview::text("hello world", true, "C:/data/a.cpp");

    EXPECT_EQ(preview.kind(), FilePreviewKind::Text);
    EXPECT_EQ(preview.text(), "hello world");
    EXPECT_TRUE(preview.textTruncated());
    EXPECT_EQ(preview.path(), "C:/data/a.cpp");
    EXPECT_TRUE(preview.folderEntries().empty());
}

TEST(FilePreview, TextFactoryDefaultsTruncatedFalseWhenNotTruncated)
{
    FilePreview preview = FilePreview::text("hello", false, "C:/data/a.txt");

    EXPECT_FALSE(preview.textTruncated());
}

TEST(FilePreview, ImageFactorySetsKindAndBytes)
{
    std::vector<std::byte> bytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };

    FilePreview preview = FilePreview::image(bytes);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Image);
    EXPECT_EQ(preview.imageBytes(), bytes);
}

TEST(FilePreview, VideoFactorySetsKindAndPosterBytes)
{
    std::vector<std::byte> bytes{ std::byte{ 0x01 }, std::byte{ 0x02 } };

    FilePreview preview = FilePreview::video(bytes);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Video);
    EXPECT_EQ(preview.imageBytes(), bytes);
}

TEST(FilePreview, UnsupportedFactorySetsKindOnly)
{
    FilePreview preview = FilePreview::unsupported();

    EXPECT_EQ(preview.kind(), FilePreviewKind::Unsupported);
    EXPECT_TRUE(preview.folderEntries().empty());
    EXPECT_TRUE(preview.text().empty());
    EXPECT_TRUE(preview.imageBytes().empty());
}
