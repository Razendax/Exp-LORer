#include <gtest/gtest.h>

#include "BulkRenameUseCase.h"
#include "MockFileSystemRepository.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    FileNode makeFile(const std::filesystem::path& path, std::uintmax_t size = 10)
    {
        return FileNode::create(path, size, std::chrono::system_clock::now(), std::chrono::system_clock::now(), FileType::Regular)
            .value();
    }
}

TEST(BulkRenameUseCase, OkForACleanRename)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(_)).WillRepeatedly(Return(Result<FileNode>::failure(Error(ErrorCode::NotFound, "not found"))));

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern;
    pattern.findText = "vacation";
    pattern.replaceText = "trip";

    std::vector<FileNode> selection{ makeFile("C:/data/vacation.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].status, BulkRenameUseCase::EntryStatus::Ok);
    EXPECT_EQ(result.value()[0].proposedPath, std::filesystem::path("C:/data/trip.jpg"));
}

TEST(BulkRenameUseCase, UnchangedWhenPatternIsANoOp)
{
    MockFileSystemRepository repository;

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern; // every stage disabled

    std::vector<FileNode> selection{ makeFile("C:/data/photo.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].status, BulkRenameUseCase::EntryStatus::Unchanged);
}

TEST(BulkRenameUseCase, CollisionWithinBatchWhenTwoEntriesMapToSameName)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(_)).WillRepeatedly(Return(Result<FileNode>::failure(Error(ErrorCode::NotFound, "not found"))));

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern;
    pattern.useRegex = true;
    pattern.findText = "\\d+";
    pattern.replaceText = "";

    std::vector<FileNode> selection{ makeFile("C:/data/img1.jpg"), makeFile("C:/data/img2.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0].status, BulkRenameUseCase::EntryStatus::CollisionWithinBatch);
    EXPECT_EQ(result.value()[1].status, BulkRenameUseCase::EntryStatus::CollisionWithinBatch);
}

TEST(BulkRenameUseCase, CollisionWithExistingWhenStatFindsAnUnrelatedSibling)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(std::filesystem::path("C:/data/trip.jpg")))
        .WillOnce(Return(Result<FileNode>::success(makeFile("C:/data/trip.jpg"))));

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern;
    pattern.findText = "vacation";
    pattern.replaceText = "trip";

    std::vector<FileNode> selection{ makeFile("C:/data/vacation.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].status, BulkRenameUseCase::EntryStatus::CollisionWithExisting);
}

TEST(BulkRenameUseCase, InvalidNameForAForbiddenCharacterIntroducedByThePattern)
{
    MockFileSystemRepository repository;

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern;
    pattern.findText = "photo";
    pattern.replaceText = "photo:bad";

    std::vector<FileNode> selection{ makeFile("C:/data/photo.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].status, BulkRenameUseCase::EntryStatus::InvalidName);
}

TEST(BulkRenameUseCase, InvalidRegexFailsTheWholePreview)
{
    MockFileSystemRepository repository;

    BulkRenameUseCase useCase(repository);
    BulkRenamePattern pattern;
    pattern.useRegex = true;
    pattern.findText = "[unterminated";

    std::vector<FileNode> selection{ makeFile("C:/data/photo.jpg") };
    auto result = useCase.computePreview(selection, pattern);

    EXPECT_TRUE(result.hasError());
}
