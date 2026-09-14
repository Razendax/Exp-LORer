#include <gtest/gtest.h>

#include "FileNavigationUseCase.h"
#include "MockFileSystemRepository.h"

using ::testing::Return;
using ::testing::_;

namespace
{
    FileNode makeFile(const std::filesystem::path& path, std::uintmax_t size, FileType type = FileType::Regular)
    {
        return FileNode::create(path, size, std::chrono::system_clock::now(), std::chrono::system_clock::now(), type)
            .value();
    }
}

TEST(FileNavigationUseCase, ListDirectoryDelegatesToRepository)
{
    MockFileSystemRepository repository;
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 10) };
    EXPECT_CALL(repository, listDirectory(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(files)));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.listDirectory("C:/data");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST(FileNavigationUseCase, ListDirectoryPropagatesError)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, listDirectory(_))
        .WillOnce(Return(Result<std::vector<FileNode>>::failure(Error(ErrorCode::IoError, "denied"))));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.listDirectory("C:/locked");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(FileNavigationUseCase, StatDelegatesToRepository)
{
    MockFileSystemRepository repository;
    FileNode file = makeFile("C:/data", 0, FileType::Directory);
    EXPECT_CALL(repository, stat(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<FileNode>::success(file)));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.stat("C:/data");

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().isDirectory());
}

TEST(FileNavigationUseCase, SortByNameAscending)
{
    std::vector<FileNode> files{ makeFile("C:/data/b.txt", 1), makeFile("C:/data/a.txt", 2) };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Name, true);

    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0].name(), "a.txt");
    EXPECT_EQ(sorted[1].name(), "b.txt");
}

TEST(FileNavigationUseCase, SortBySizeDescending)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.txt", 100) };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Size, false);

    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0].size(), 100u);
    EXPECT_EQ(sorted[1].size(), 1u);
}

TEST(FileNavigationUseCase, FilterByExtensionIsCaseInsensitive)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.TXT", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByExtension(files, ".txt");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "a.TXT");
}

TEST(FileNavigationUseCase, MoveFileDelegatesToRepository)
{
    MockFileSystemRepository repository;
    FileNode moved = makeFile("C:/data/dest.txt", 5);
    EXPECT_CALL(repository, move(std::filesystem::path("C:/data/src.txt"), std::filesystem::path("C:/data/dest.txt")))
        .WillOnce(Return(Result<FileNode>::success(moved)));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.moveFile("C:/data/src.txt", "C:/data/dest.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().name(), "dest.txt");
}

TEST(FileNavigationUseCase, MoveFileToTrashDelegatesToRepository)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, moveToTrash(std::filesystem::path("C:/data/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.moveFileToTrash("C:/data/a.txt");

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, DeleteFilePermanentlyDelegatesToRepository)
{
    MockFileSystemRepository repository;
    EXPECT_CALL(repository, deletePermanently(std::filesystem::path("C:/data/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository);
    auto result = useCase.deleteFilePermanently("C:/data/a.txt");

    EXPECT_TRUE(result.hasValue());
}
