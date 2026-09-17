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

TEST(FileNavigationUseCase, ListDirectoryRecursiveDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    std::vector<FileNode> files{ makeFile("C:/data/sub/a.txt", 10) };
    EXPECT_CALL(repository, listDirectoryRecursive(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(files)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.listDirectoryRecursive("C:/data");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
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

TEST(FileNavigationUseCase, FilterByNameEmptyQueryMatchesEverything)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "");

    EXPECT_EQ(filtered.size(), 2u);
}

TEST(FileNavigationUseCase, FilterByNameIsCaseInsensitive)
{
    std::vector<FileNode> files{ makeFile("C:/data/Report.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "REPORT");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "Report.txt");
}

TEST(FileNavigationUseCase, FilterByNameMatchesSubstringMidName)
{
    std::vector<FileNode> files{ makeFile("C:/data/quarterly_report_final.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "report");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "quarterly_report_final.txt");
}

TEST(FileNavigationUseCase, FilterByNameReturnsEmptyWhenNoMatches)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "nomatch");

    EXPECT_TRUE(filtered.empty());
}

TEST(FileNavigationUseCase, FilterBySizeRangeNoBoundsIsPassthrough)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/folder", 0, FileType::Directory) };

    auto filtered = FileNavigationUseCase::filterBySizeRange(files, std::nullopt, std::nullopt);

    EXPECT_EQ(filtered.size(), 2u);
}

TEST(FileNavigationUseCase, FilterBySizeRangeExcludesDirectories)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 10), makeFile("C:/data/folder", 0, FileType::Directory) };

    auto filtered = FileNavigationUseCase::filterBySizeRange(files, 0, std::nullopt);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "a.txt");
}

TEST(FileNavigationUseCase, FilterBySizeRangeAppliesMinAndMaxBounds)
{
    std::vector<FileNode> files{
        makeFile("C:/data/small.txt", 5),
        makeFile("C:/data/mid.txt", 50),
        makeFile("C:/data/large.txt", 500),
    };

    auto filtered = FileNavigationUseCase::filterBySizeRange(files, 10, 100);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "mid.txt");
}

TEST(FileNavigationUseCase, FilterByExtensionsEmptyListIsPassthrough)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/folder", 0, FileType::Directory) };

    auto filtered = FileNavigationUseCase::filterByExtensions(files, {});

    EXPECT_EQ(filtered.size(), 2u);
}

TEST(FileNavigationUseCase, FilterByExtensionsOrsTokensCaseInsensitivelyAndExcludesDirectories)
{
    std::vector<FileNode> files{
        makeFile("C:/data/a.JPG", 1),
        makeFile("C:/data/b.png", 2),
        makeFile("C:/data/c.txt", 3),
        makeFile("C:/data/folder.jpg", 0, FileType::Directory),
    };

    auto filtered = FileNavigationUseCase::filterByExtensions(files, { ".jpg", ".png" });

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].name(), "a.JPG");
    EXPECT_EQ(filtered[1].name(), "b.png");
}

TEST(FileNavigationUseCase, FilterByCriteriaComposesAllThreeFilters)
{
    std::vector<FileNode> files{
        makeFile("C:/data/report.jpg", 50),
        makeFile("C:/data/report.png", 5),
        makeFile("C:/data/report.txt", 50),
        makeFile("C:/data/other.jpg", 50),
    };

    SearchCriteria criteria;
    criteria.nameQuery = "report";
    criteria.minSizeBytes = 10;
    criteria.extensionList = "jpg, png";

    auto filtered = FileNavigationUseCase::filterByCriteria(files, criteria);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "report.jpg");
}

TEST(FileNavigationUseCase, FilterByCriteriaAllUnsetIsPassthrough)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/folder", 0, FileType::Directory) };

    SearchCriteria criteria;
    auto filtered = FileNavigationUseCase::filterByCriteria(files, criteria);

    EXPECT_EQ(filtered.size(), 2u);
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

TEST(FileNavigationUseCase, BuildItemContextMenuDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    const std::vector<std::filesystem::path> paths{ "C:/data/a.txt" };

    std::vector<ContextMenuEntry> entries{ ContextMenuEntry{ 1, "Open", false, true, {}, {} } };
    EXPECT_CALL(contextMenuProvider, buildItemMenu(paths, ContextMenuSourceMode::StaticVerbsOnly))
        .WillOnce(Return(Result<std::vector<ContextMenuEntry>>::success(entries)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.buildItemContextMenu(paths, ContextMenuSourceMode::StaticVerbsOnly);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST(FileNavigationUseCase, BuildBackgroundContextMenuDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    const std::filesystem::path folder = "C:/data";

    EXPECT_CALL(contextMenuProvider, buildBackgroundMenu(folder, ContextMenuSourceMode::StaticAndShellExtensions))
        .WillOnce(Return(Result<std::vector<ContextMenuEntry>>::success(std::vector<ContextMenuEntry>{})));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.buildBackgroundContextMenu(folder, ContextMenuSourceMode::StaticAndShellExtensions);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, InvokeContextMenuEntryDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    NativeWindowHandle ownerWindow = reinterpret_cast<NativeWindowHandle>(0x1234);

    EXPECT_CALL(contextMenuProvider, invoke(42u, ownerWindow)).WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.invokeContextMenuEntry(42u, ownerWindow);

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, DiscardContextMenuDelegatesToProvider)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;

    EXPECT_CALL(contextMenuProvider, discardMenu()).Times(1);

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    useCase.discardContextMenu();
}

TEST(FileNavigationUseCase, CreateFolderDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;

    EXPECT_CALL(repository, createDirectory(std::filesystem::path("C:/data/New folder")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.createFolder("C:/data/New folder");

    EXPECT_TRUE(result.hasValue());
}

TEST(FileNavigationUseCase, CreateFileFromTemplateDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    FileNode created = makeFile("C:/data/New file.txt", 0);

    EXPECT_CALL(repository, createFileFromTemplate(std::filesystem::path("C:/data/New file.txt"),
                                                     std::optional<std::filesystem::path>(std::nullopt)))
        .WillOnce(Return(Result<FileNode>::success(created)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.createFileFromTemplate("C:/data/New file.txt", std::nullopt);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().name(), "New file.txt");
}

TEST(FileNavigationUseCase, ShowPropertiesDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    NativeWindowHandle ownerWindow = reinterpret_cast<NativeWindowHandle>(0x1234);

    EXPECT_CALL(repository, showProperties(std::filesystem::path("C:/data/a.txt"), ownerWindow))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.showProperties("C:/data/a.txt", ownerWindow);

    EXPECT_TRUE(result.hasValue());
}
