#include <gtest/gtest.h>

#include <algorithm>

#include "MockFileSystemRepository.h"
#include "TextFileEditUseCase.h"

using ::testing::Return;
using ::testing::_;

namespace
{
    std::vector<std::byte> toBytes(const std::string& s)
    {
        std::vector<std::byte> bytes(s.size());
        std::transform(s.begin(), s.end(), bytes.begin(), [](char c) { return std::byte(c); });
        return bytes;
    }
}

TEST(TextFileEditUseCase, ReadFullTextReadsFileDirectlyWhenNotInsideArchive)
{
    MockFileSystemRepository fileSystemRepository;

    EXPECT_CALL(fileSystemRepository, readFile(std::filesystem::path("C:/data/notes.txt")))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("hello world"))));

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.readFullText("C:/data/notes.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "hello world");
}

TEST(TextFileEditUseCase, ReadFullTextPropagatesReadFailure)
{
    MockFileSystemRepository fileSystemRepository;

    EXPECT_CALL(fileSystemRepository, readFile(_))
        .WillOnce(Return(Result<std::vector<std::byte>>::failure(Error(ErrorCode::IoError, "boom"))));

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.readFullText("C:/data/notes.txt");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

// Archive browsing (Architecture.md §14.28): a path nested inside a real archive has no real file
// on disk -- readFullText must materialize it to a real temp file before reading, rather than
// passing the virtual path straight to readFile, same precedent FilePreviewUseCase follows.
TEST(TextFileEditUseCase, ReadFullTextMaterializesBeforeReadingWhenInsideArchive)
{
    MockFileSystemRepository fileSystemRepository;

    const std::filesystem::path virtualPath("C:/data/docs.zip/readme.txt");
    const std::filesystem::path realPath("C:/temp/archive-preview/1/readme.txt");

    EXPECT_CALL(fileSystemRepository, materializeForReading(virtualPath))
        .WillOnce(Return(Result<std::filesystem::path>::success(realPath)));
    EXPECT_CALL(fileSystemRepository, readFile(realPath))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("hello world"))));

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.readFullText(virtualPath);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "hello world");
}

TEST(TextFileEditUseCase, ReadFullTextPropagatesMaterializeFailure)
{
    MockFileSystemRepository fileSystemRepository;

    const std::filesystem::path virtualPath("C:/data/docs.zip/readme.txt");

    EXPECT_CALL(fileSystemRepository, materializeForReading(virtualPath))
        .WillOnce(Return(Result<std::filesystem::path>::failure(Error(ErrorCode::IoError, "extraction failed"))));
    EXPECT_CALL(fileSystemRepository, readFile(_)).Times(0);

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.readFullText(virtualPath);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

// writeText must target the original path directly, never materialized -- a save has to land on
// the real file, not a temp-extracted archive copy (Architecture.md §14.98).
TEST(TextFileEditUseCase, WriteTextWritesToOriginalPathWithoutMaterializing)
{
    MockFileSystemRepository fileSystemRepository;

    EXPECT_CALL(fileSystemRepository, writeFile(std::filesystem::path("C:/data/notes.txt"), toBytes("updated content")))
        .WillOnce(Return(Result<void>::success()));
    EXPECT_CALL(fileSystemRepository, materializeForReading(_)).Times(0);

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.writeText("C:/data/notes.txt", "updated content");

    EXPECT_TRUE(result.hasValue());
}

TEST(TextFileEditUseCase, WriteTextPropagatesWriteFailure)
{
    MockFileSystemRepository fileSystemRepository;

    EXPECT_CALL(fileSystemRepository, writeFile(_, _))
        .WillOnce(Return(Result<void>::failure(Error(ErrorCode::IoError, "disk full"))));

    TextFileEditUseCase useCase(fileSystemRepository);
    auto result = useCase.writeText("C:/data/notes.txt", "updated content");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}
