#include <gtest/gtest.h>

#include <algorithm>

#include "FilePreviewUseCase.h"
#include "MockFileSystemRepository.h"
#include "MockMediaDecoder.h"

using ::testing::Return;
using ::testing::_;

namespace
{
    FileNode makeNode(const std::filesystem::path& path, FileType type)
    {
        return FileNode::create(path, 10, std::chrono::system_clock::now(), std::chrono::system_clock::now(), type).value();
    }

    std::vector<std::byte> toBytes(const std::string& s)
    {
        std::vector<std::byte> bytes(s.size());
        std::transform(s.begin(), s.end(), bytes.begin(), [](char c) { return std::byte(c); });
        return bytes;
    }
}

TEST(FilePreviewUseCase, DirectoryTargetListsAndSortsDirectoriesFirst)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    std::vector<FileNode> listing{
        makeNode("C:/data/zeta.txt", FileType::Regular),
        makeNode("C:/data/Beta", FileType::Directory),
        makeNode("C:/data/alpha.txt", FileType::Regular),
        makeNode("C:/data/alpha", FileType::Directory),
    };
    EXPECT_CALL(fileSystemRepository, listDirectory(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data", FileType::Directory), 128, 128, 1024);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().kind(), FilePreviewKind::Folder);
    const std::vector<FileNode>& sorted = result.value().folderEntries();
    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0].name(), "alpha");
    EXPECT_EQ(sorted[1].name(), "Beta");
    EXPECT_EQ(sorted[2].name(), "alpha.txt");
    EXPECT_EQ(sorted[3].name(), "zeta.txt");
}

TEST(FilePreviewUseCase, DirectoryTargetPropagatesListDirectoryFailure)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    EXPECT_CALL(fileSystemRepository, listDirectory(_))
        .WillOnce(Return(Result<std::vector<FileNode>>::failure(Error(ErrorCode::NotFound, "gone"))));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data", FileType::Directory), 128, 128, 1024);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(FilePreviewUseCase, ImageExtensionDispatchesToMediaDecoderAsImage)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    std::vector<std::byte> jpegBytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };
    EXPECT_CALL(mediaDecoder, generateThumbnail(std::filesystem::path("C:/data/photo.JPG"), 200, 200))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(jpegBytes)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/photo.JPG", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Image);
    EXPECT_EQ(result.value().imageBytes(), jpegBytes);
}

TEST(FilePreviewUseCase, VideoExtensionDispatchesToMediaDecoderAsVideo)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    std::vector<std::byte> posterBytes{ std::byte{ 0x01 } };
    EXPECT_CALL(mediaDecoder, generateThumbnail(std::filesystem::path("C:/data/clip.mp4"), 200, 200))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(posterBytes)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/clip.mp4", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Video);
    EXPECT_EQ(result.value().imageBytes(), posterBytes);
}

TEST(FilePreviewUseCase, TextExtensionReadsPrefixAndReturnsText)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    EXPECT_CALL(fileSystemRepository, readFilePrefix(std::filesystem::path("C:/data/notes.txt"), 1024u))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("hello world"))));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/notes.txt", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Text);
    EXPECT_EQ(result.value().text(), "hello world");
    EXPECT_FALSE(result.value().textTruncated());
    // PreviewPanelWidget::showText() derives the syntax-highlighting language from this path
    // (Architecture.md §14.26) -- generatePreview() must thread the target's own path through
    // unchanged, not an empty/default-constructed one.
    EXPECT_EQ(result.value().path(), std::filesystem::path("C:/data/notes.txt"));
}

TEST(FilePreviewUseCase, TextExtensionMarksTruncatedWhenFileIsLargerThanCap)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    // makeNode() reports a 10-byte file; the cap below (5) is smaller than that, so the prefix
    // read fills the cap and the true file size confirms there is more content beyond it.
    EXPECT_CALL(fileSystemRepository, readFilePrefix(_, 5u))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("hello"))));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/notes.txt", FileType::Regular), 200, 200, 5);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().textTruncated());
}

TEST(FilePreviewUseCase, TextExtensionNotTruncatedWhenFileSizeExactlyFillsCap)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    // The file's true size (10, per makeNode()) exactly equals the cap, so the prefix read
    // captured the entire file even though it filled the cap -- this must not be reported as
    // truncated.
    EXPECT_CALL(fileSystemRepository, readFilePrefix(_, 10u))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("0123456789"))));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/notes.txt", FileType::Regular), 200, 200, 10);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().textTruncated());
}

TEST(FilePreviewUseCase, TextExtensionWithNulByteBecomesUnsupported)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    std::vector<std::byte> withNul = toBytes("abc");
    withNul.push_back(std::byte{ 0 });
    withNul.push_back(std::byte{ 'd' });
    EXPECT_CALL(fileSystemRepository, readFilePrefix(_, _))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(withNul)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/binaryish.log", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Unsupported);
}

TEST(FilePreviewUseCase, UnknownExtensionReturnsUnsupportedWithoutTouchingPorts)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    EXPECT_CALL(fileSystemRepository, listDirectory(_)).Times(0);
    EXPECT_CALL(fileSystemRepository, readFilePrefix(_, _)).Times(0);
    EXPECT_CALL(mediaDecoder, generateThumbnail(_, _, _)).Times(0);

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/unknown.xyz", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Unsupported);
}

// Archive browsing (Architecture.md §14.28): a not-yet-entered archive file (Regular FileType, not
// Directory) gets the same folder treatment a real directory does, since listDirectory() on its
// own path already returns its top-level entries once StandardFileSystemRepository resolves it.
// A file selected while browsing *inside* an archive (Architecture.md §14.28) has a virtual path
// with no real file on disk -- generatePreview() must materialize it to a real temp file before
// handing it to the media decoder, rather than passing the virtual path straight through.
TEST(FilePreviewUseCase, ImageInsideArchiveIsMaterializedBeforeDecoding)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    const std::filesystem::path virtualPath("C:/data/photos.zip/vacation/img.jpg");
    const std::filesystem::path realPath("C:/temp/archive-preview/1/img.jpg");

    EXPECT_CALL(fileSystemRepository, materializeForReading(virtualPath))
        .WillOnce(Return(Result<std::filesystem::path>::success(realPath)));

    std::vector<std::byte> jpegBytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };
    EXPECT_CALL(mediaDecoder, generateThumbnail(realPath, 200, 200))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(jpegBytes)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode(virtualPath, FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Image);
    EXPECT_EQ(result.value().imageBytes(), jpegBytes);
}

TEST(FilePreviewUseCase, TextInsideArchiveIsMaterializedBeforeReading)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    const std::filesystem::path virtualPath("C:/data/docs.zip/readme.txt");
    const std::filesystem::path realPath("C:/temp/archive-preview/2/readme.txt");

    EXPECT_CALL(fileSystemRepository, materializeForReading(virtualPath))
        .WillOnce(Return(Result<std::filesystem::path>::success(realPath)));
    EXPECT_CALL(fileSystemRepository, readFilePrefix(realPath, 1024u))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("hello world"))));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode(virtualPath, FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().kind(), FilePreviewKind::Text);
    EXPECT_EQ(result.value().text(), "hello world");
    // Syntax highlighting still keys off the target's own (virtual) path, not the materialized
    // temp path.
    EXPECT_EQ(result.value().path(), virtualPath);
}

TEST(FilePreviewUseCase, ImageInsideArchivePropagatesMaterializeFailure)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    const std::filesystem::path virtualPath("C:/data/photos.zip/vacation/img.jpg");

    EXPECT_CALL(fileSystemRepository, materializeForReading(virtualPath))
        .WillOnce(Return(Result<std::filesystem::path>::failure(Error(ErrorCode::IoError, "extraction failed"))));
    EXPECT_CALL(mediaDecoder, generateThumbnail(_, _, _)).Times(0);

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode(virtualPath, FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(FilePreviewUseCase, ArchiveExtensionDispatchesToListDirectoryAsFolder)
{
    MockFileSystemRepository fileSystemRepository;
    MockMediaDecoder mediaDecoder;

    std::vector<FileNode> listing{
        makeNode("C:/data/photos.zip/readme.txt", FileType::Regular),
        makeNode("C:/data/photos.zip/vacation", FileType::Directory),
    };
    EXPECT_CALL(fileSystemRepository, listDirectory(std::filesystem::path("C:/data/photos.zip")))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));

    FilePreviewUseCase useCase(fileSystemRepository, mediaDecoder);
    auto result = useCase.generatePreview(makeNode("C:/data/photos.zip", FileType::Regular), 200, 200, 1024);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().kind(), FilePreviewKind::Folder);
    EXPECT_EQ(result.value().folderEntries().size(), 2u);
}
