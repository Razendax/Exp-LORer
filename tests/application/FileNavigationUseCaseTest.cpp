#include <gtest/gtest.h>

#include "FileNavigationUseCase.h"
#include "MockContextMenuProvider.h"
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
    MockContextMenuProvider contextMenuProvider;
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 10) };
    EXPECT_CALL(repository, listDirectory(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(files)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.listDirectory("C:/data");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST(FileNavigationUseCase, ListDirectoryPropagatesError)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    EXPECT_CALL(repository, listDirectory(_))
        .WillOnce(Return(Result<std::vector<FileNode>>::failure(Error(ErrorCode::IoError, "denied"))));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.listDirectory("C:/locked");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(FileNavigationUseCase, StatDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    FileNode file = makeFile("C:/data", 0, FileType::Directory);
    EXPECT_CALL(repository, stat(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<FileNode>::success(file)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
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

TEST(FileNavigationUseCase, SortByNameAscendingGroupsFoldersBeforeFiles)
{
    std::vector<FileNode> files{
        makeFile("C:/data/b.txt", 1),
        makeFile("C:/data/z_folder", 0, FileType::Directory),
        makeFile("C:/data/a.txt", 2),
        makeFile("C:/data/a_folder", 0, FileType::Directory),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Name, true);

    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0].name(), "a_folder");
    EXPECT_EQ(sorted[1].name(), "z_folder");
    EXPECT_EQ(sorted[2].name(), "a.txt");
    EXPECT_EQ(sorted[3].name(), "b.txt");
}

TEST(FileNavigationUseCase, SortByNameDescendingGroupsFilesBeforeFolders)
{
    std::vector<FileNode> files{
        makeFile("C:/data/b.txt", 1),
        makeFile("C:/data/z_folder", 0, FileType::Directory),
        makeFile("C:/data/a.txt", 2),
        makeFile("C:/data/a_folder", 0, FileType::Directory),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Name, false);

    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0].name(), "b.txt");
    EXPECT_EQ(sorted[1].name(), "a.txt");
    EXPECT_EQ(sorted[2].name(), "z_folder");
    EXPECT_EQ(sorted[3].name(), "a_folder");
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
    MockContextMenuProvider contextMenuProvider;
    FileNode moved = makeFile("C:/data/dest.txt", 5);
    EXPECT_CALL(repository, move(std::filesystem::path("C:/data/src.txt"), std::filesystem::path("C:/data/dest.txt")))
        .WillOnce(Return(Result<FileNode>::success(moved)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.moveFile("C:/data/src.txt", "C:/data/dest.txt");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().name(), "dest.txt");
}

TEST(FileNavigationUseCase, MoveFileToTrashDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    EXPECT_CALL(repository, moveToTrash(std::filesystem::path("C:/data/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.moveFileToTrash("C:/data/a.txt");

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, DeleteFilePermanentlyDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    EXPECT_CALL(repository, deletePermanently(std::filesystem::path("C:/data/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.deleteFilePermanently("C:/data/a.txt");

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, OpenFileDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    EXPECT_CALL(repository, openWithDefaultApplication(std::filesystem::path("C:/data/a.txt")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.openFile("C:/data/a.txt");

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, ShowItemContextMenuDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    const std::vector<std::filesystem::path> paths{ "C:/data/a.txt" };
    const NativeScreenPoint screenPosition{ 10, 20 };
    NativeWindowHandle ownerWindow = reinterpret_cast<NativeWindowHandle>(0x1234);

    EXPECT_CALL(contextMenuProvider, showItemContextMenu(paths, testing::_, ownerWindow))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.showItemContextMenu(paths, screenPosition, ownerWindow);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, ShowFolderBackgroundContextMenuDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    const std::filesystem::path folder = "C:/data";
    const NativeScreenPoint screenPosition{ 10, 20 };
    NativeWindowHandle ownerWindow = reinterpret_cast<NativeWindowHandle>(0x1234);

    EXPECT_CALL(contextMenuProvider, showBackgroundContextMenu(folder, testing::_, ownerWindow))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.showFolderBackgroundContextMenu(folder, screenPosition, ownerWindow);

    EXPECT_TRUE(result.hasValue());
}
