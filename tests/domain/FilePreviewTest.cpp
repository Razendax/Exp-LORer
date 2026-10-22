#include <gtest/gtest.h>

#include "FilePreview.h"
#include "MediaMetadata.h"

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

TEST(FilePreview, ImageFactorySetsKindBytesPathAndMetadata)
{
    std::vector<std::byte> bytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };
    MediaMetadata metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(0), "jpeg").value();

    FilePreview preview = FilePreview::image(bytes, "C:/data/photo.jpg", metadata, 2048);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Image);
    EXPECT_EQ(preview.imageBytes(), bytes);
    EXPECT_EQ(preview.path(), "C:/data/photo.jpg");
    EXPECT_EQ(preview.metadata(), metadata);
    EXPECT_EQ(preview.fileSizeBytes(), 2048u);
}

TEST(FilePreview, VideoFactorySetsKindPosterBytesPathAndMetadata)
{
    std::vector<std::byte> bytes{ std::byte{ 0x01 }, std::byte{ 0x02 } };
    MediaMetadata metadata = MediaMetadata::create(1280, 720, std::chrono::milliseconds(60000), "h264", 5'000'000).value();

    FilePreview preview = FilePreview::video(bytes, "C:/data/clip.mp4", metadata, 4096);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Video);
    EXPECT_EQ(preview.imageBytes(), bytes);
    EXPECT_EQ(preview.path(), "C:/data/clip.mp4");
    EXPECT_EQ(preview.metadata(), metadata);
    EXPECT_EQ(preview.fileSizeBytes(), 4096u);
}

TEST(FilePreview, PdfFactorySetsKindBytesPathAndMetadata)
{
    std::vector<std::byte> bytes{ std::byte{ 0x25 }, std::byte{ 0x50 } };
    MediaMetadata metadata = MediaMetadata::create(800, 1200, std::chrono::milliseconds(0), "pdf", 0, 3).value();

    FilePreview preview = FilePreview::pdf(bytes, "C:/data/doc.pdf", metadata, 1024);

    EXPECT_EQ(preview.kind(), FilePreviewKind::Pdf);
    EXPECT_EQ(preview.imageBytes(), bytes);
    EXPECT_EQ(preview.path(), "C:/data/doc.pdf");
    EXPECT_EQ(preview.metadata(), metadata);
    EXPECT_EQ(preview.metadata().pageCount(), 3);
    EXPECT_EQ(preview.fileSizeBytes(), 1024u);
    EXPECT_TRUE(preview.text().empty());
    EXPECT_TRUE(preview.folderEntries().empty());
}

TEST(FilePreview, UnsupportedFactorySetsKindOnly)
{
    FilePreview preview = FilePreview::unsupported();

    EXPECT_EQ(preview.kind(), FilePreviewKind::Unsupported);
    EXPECT_TRUE(preview.folderEntries().empty());
    EXPECT_TRUE(preview.text().empty());
    EXPECT_TRUE(preview.imageBytes().empty());
    EXPECT_EQ(preview.metadata().width(), 0);
    EXPECT_EQ(preview.metadata().height(), 0);
}
