#include <gtest/gtest.h>

#include "FileTransferUseCase.h"
#include "MockFileSystemRepository.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    FileNode makeFile(const std::filesystem::path& path, std::uintmax_t size, FileType type = FileType::Regular)
    {
        return FileNode::create(path, size, std::chrono::system_clock::now(), std::chrono::system_clock::now(), type).value();
    }
}

TEST(FileTransferUseCase, PlanTransferSingleFileAddsOneItem)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(std::filesystem::path("C:/src/a.txt")))
        .WillOnce(Return(Result<FileNode>::success(makeFile("C:/src/a.txt", 100))));

    FileTransferUseCase useCase(repository);
    auto result = useCase.planTransfer({ { "C:/src/a.txt", "C:/dest/a.txt" } }, nullptr, nullptr);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().items.size(), 1u);
    EXPECT_EQ(result.value().items[0].source, std::filesystem::path("C:/src/a.txt"));
    EXPECT_EQ(result.value().items[0].destination, std::filesystem::path("C:/dest/a.txt"));
    EXPECT_FALSE(result.value().items[0].isDirectory);
    EXPECT_EQ(result.value().totalFileCount, 1u);
    EXPECT_EQ(result.value().totalBytes, 100u);
}

TEST(FileTransferUseCase, PlanTransferDirectoryWalksRecursivelyAndRemapsDestinations)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(std::filesystem::path("C:/src/folder")))
        .WillOnce(Return(Result<FileNode>::success(makeFile("C:/src/folder", 0, FileType::Directory))));

    std::vector<FileNode> descendants{
        makeFile("C:/src/folder/sub", 0, FileType::Directory),
        makeFile("C:/src/folder/sub/b.txt", 50),
    };
    EXPECT_CALL(repository, listDirectoryRecursive(std::filesystem::path("C:/src/folder"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(descendants)));

    FileTransferUseCase useCase(repository);
    auto result = useCase.planTransfer({ { "C:/src/folder", "C:/dest/folder" } }, nullptr, nullptr);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().items.size(), 3u);
    EXPECT_EQ(result.value().totalFileCount, 1u);
    EXPECT_EQ(result.value().totalBytes, 50u);

    const auto& fileItem = result.value().items[2];
    EXPECT_EQ(fileItem.destination, std::filesystem::path("C:/dest/folder/sub/b.txt"));
}

TEST(FileTransferUseCase, PlanTransferPropagatesStatError)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, stat(_)).WillOnce(Return(Result<FileNode>::failure(Error(ErrorCode::NotFound, "gone"))));

    FileTransferUseCase useCase(repository);
    auto result = useCase.planTransfer({ { "C:/src/missing.txt", "C:/dest/missing.txt" } }, nullptr, nullptr);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(FileTransferUseCase, CheckDestinationHasSpacePassesWhenEnoughFreeSpace)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, driveSpaceInfo(_))
        .WillOnce(Return(Result<DriveSpaceInfo>::success(DriveSpaceInfo{ "C:", 1000, 2000 })));

    FileTransferUseCase useCase(repository);
    auto result = useCase.checkDestinationHasSpace("C:/dest", 500);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileTransferUseCase, CheckDestinationHasSpaceFailsWhenNotEnoughFreeSpace)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, driveSpaceInfo(_))
        .WillOnce(Return(Result<DriveSpaceInfo>::success(DriveSpaceInfo{ "C:", 100, 2000 })));

    FileTransferUseCase useCase(repository);
    auto result = useCase.checkDestinationHasSpace("C:/dest", 500);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InsufficientDiskSpace);
}

TEST(FileTransferUseCase, CheckDestinationHasSpaceTreatsUnresolvableDriveAsAllowed)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, driveSpaceInfo(_)).WillOnce(Return(Result<DriveSpaceInfo>::failure(Error(ErrorCode::NotFound, "no drive"))));

    FileTransferUseCase useCase(repository);
    auto result = useCase.checkDestinationHasSpace("this-pc:", 500);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileTransferUseCase, ExecuteTransferCopiesFilesAndCreatesDirectories)
{
    MockFileSystemRepository repository;

    FileTransferPlan plan;
    plan.items = {
        FileTransferItem{ "C:/src/folder", "C:/dest/folder", true, 0 },
        FileTransferItem{ "C:/src/folder/a.txt", "C:/dest/folder/a.txt", false, 10 },
    };
    plan.totalFileCount = 1;
    plan.totalBytes = 10;

    EXPECT_CALL(repository, createDirectory(std::filesystem::path("C:/dest/folder")))
        .WillOnce(Return(Result<void>::success()));
    EXPECT_CALL(repository, copyFileWithProgress(std::filesystem::path("C:/src/folder/a.txt"),
                                                   std::filesystem::path("C:/dest/folder/a.txt"), _))
        .WillOnce(Return(Result<FileNode>::success(makeFile("C:/dest/folder/a.txt", 10))));
    EXPECT_CALL(repository, deletePermanently(_)).Times(0);

    FileTransferUseCase useCase(repository);
    FileTransferCallbacks callbacks;
    auto result = useCase.executeTransfer(plan, { { "C:/src/folder", "C:/dest/folder" } }, false, callbacks);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileTransferUseCase, ExecuteTransferDeletesSourceOnlyWhenMoveAndFullyCopied)
{
    MockFileSystemRepository repository;

    FileTransferPlan plan;
    plan.items = { FileTransferItem{ "C:/src/a.txt", "C:/dest/a.txt", false, 10 } };
    plan.totalFileCount = 1;
    plan.totalBytes = 10;

    EXPECT_CALL(repository, copyFileWithProgress(_, _, _)).WillOnce(Return(Result<FileNode>::success(makeFile("C:/dest/a.txt", 10))));
    EXPECT_CALL(repository, deletePermanently(std::filesystem::path("C:/src/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileTransferUseCase useCase(repository);
    FileTransferCallbacks callbacks;
    auto result = useCase.executeTransfer(plan, { { "C:/src/a.txt", "C:/dest/a.txt" } }, true, callbacks);

    EXPECT_TRUE(result.hasValue());
}

// Regression: cancellation stops before starting the next top-level root's files, but a root that
// already finished copying every one of its items keeps its move-deletion -- "best effort so far",
// not an all-or-nothing transaction (Architecture.md §14.47).
TEST(FileTransferUseCase, ExecuteTransferCancellationKeepsEarlierCompletedRootsButSkipsLaterOnes)
{
    MockFileSystemRepository repository;

    FileTransferPlan plan;
    plan.items = {
        FileTransferItem{ "C:/src/a.txt", "C:/dest/a.txt", false, 10 },
        FileTransferItem{ "C:/src/b.txt", "C:/dest/b.txt", false, 10 },
    };
    plan.totalFileCount = 2;
    plan.totalBytes = 20;

    EXPECT_CALL(repository, copyFileWithProgress(std::filesystem::path("C:/src/a.txt"), _, _))
        .WillOnce(Return(Result<FileNode>::success(makeFile("C:/dest/a.txt", 10))));
    EXPECT_CALL(repository, copyFileWithProgress(std::filesystem::path("C:/src/b.txt"), _, _)).Times(0);
    EXPECT_CALL(repository, deletePermanently(std::filesystem::path("C:/src/a.txt")))
        .WillOnce(Return(Result<void>::success()));
    EXPECT_CALL(repository, deletePermanently(std::filesystem::path("C:/src/b.txt"))).Times(0);

    int callCount = 0;
    FileTransferCallbacks callbacks;
    callbacks.shouldCancel = [&callCount]() {
        // Allow the first root (a.txt) through; cancel right before the second root (b.txt) starts.
        return callCount++ > 0;
    };

    FileTransferUseCase useCase(repository);
    auto result = useCase.executeTransfer(
        plan, { { "C:/src/a.txt", "C:/dest/a.txt" }, { "C:/src/b.txt", "C:/dest/b.txt" } }, true, callbacks);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(FileTransferUseCase, ExecuteTransferPropagatesCopyFailureWithoutDeletingSource)
{
    MockFileSystemRepository repository;

    FileTransferPlan plan;
    plan.items = { FileTransferItem{ "C:/src/a.txt", "C:/dest/a.txt", false, 10 } };
    plan.totalFileCount = 1;
    plan.totalBytes = 10;

    EXPECT_CALL(repository, copyFileWithProgress(_, _, _))
        .WillOnce(Return(Result<FileNode>::failure(Error(ErrorCode::IoError, "disk error"))));
    EXPECT_CALL(repository, deletePermanently(_)).Times(0);

    FileTransferUseCase useCase(repository);
    FileTransferCallbacks callbacks;
    auto result = useCase.executeTransfer(plan, { { "C:/src/a.txt", "C:/dest/a.txt" } }, true, callbacks);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}
