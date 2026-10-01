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
    EXPECT_CALL(repository, listDirectoryRecursive(std::filesystem::path("C:/data"), _))
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

TEST(FileNavigationUseCase, SortByNameIsCaseInsensitive)
{
    std::vector<FileNode> files{
        makeFile("C:/data/Banana.txt", 1),
        makeFile("C:/data/apple.txt", 2),
        makeFile("C:/data/cherry.txt", 3),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Name, true);

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].name(), "apple.txt");
    EXPECT_EQ(sorted[1].name(), "Banana.txt");
    EXPECT_EQ(sorted[2].name(), "cherry.txt");
}

TEST(FileNavigationUseCase, SortBySizeDescending)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.txt", 100) };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Size, false);

    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0].size(), 100u);
    EXPECT_EQ(sorted[1].size(), 1u);
}

// Regression: Size (and Percent, which orders by the same byte count) must not apply the
// "directories first" grouping -- folders and files should be interleaved purely by size.
TEST(FileNavigationUseCase, SortBySizeDoesNotGroupFoldersBeforeFiles)
{
    std::vector<FileNode> files{
        makeFile("C:/data/small_folder", 10, FileType::Directory),
        makeFile("C:/data/big.txt", 100),
        makeFile("C:/data/small.txt", 1),
        makeFile("C:/data/big_folder", 50, FileType::Directory),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Size, true);

    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0].name(), "small.txt");
    EXPECT_EQ(sorted[1].name(), "small_folder");
    EXPECT_EQ(sorted[2].name(), "big_folder");
    EXPECT_EQ(sorted[3].name(), "big.txt");
}

// Regression: a directory's FileNode::size() is always 0 (StandardFileSystemRepository never
// recurses into it while listing), so without an override every directory ties at 0 and
// std::stable_sort leaves them in their prior relative order -- looking like "folders aren't
// sorted at all". A caller that knows each directory's actual recursive size (FileListModel's
// folder-size cache) must be able to sort by it via the `sizeOf` override.
TEST(FileNavigationUseCase, SortBySizeUsesProvidedSizeResolverForDirectories)
{
    std::vector<FileNode> files{
        makeFile("C:/data/folder_a", 0, FileType::Directory),
        makeFile("C:/data/folder_b", 0, FileType::Directory),
        makeFile("C:/data/folder_c", 0, FileType::Directory),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Size, true, [](const FileNode& node) -> std::uintmax_t {
        if (node.name() == "folder_a") return 300;
        if (node.name() == "folder_b") return 100;
        return 200;
    });

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].name(), "folder_b");
    EXPECT_EQ(sorted[1].name(), "folder_c");
    EXPECT_EQ(sorted[2].name(), "folder_a");
}

TEST(FileNavigationUseCase, SortByPercentOrdersLikeSizeWithoutGroupingFolders)
{
    std::vector<FileNode> files{
        makeFile("C:/data/folder", 100, FileType::Directory),
        makeFile("C:/data/a.txt", 1),
        makeFile("C:/data/b.txt", 50),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::Percent, false);

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].name(), "folder");
    EXPECT_EQ(sorted[1].name(), "b.txt");
    EXPECT_EQ(sorted[2].name(), "a.txt");
}

TEST(FileNavigationUseCase, SortByFileTypeOrdersByExtension)
{
    std::vector<FileNode> files{
        makeFile("C:/data/a.txt", 1),
        makeFile("C:/data/b.jpg", 2),
        makeFile("C:/data/c.avi", 3),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::FileType, true);

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].name(), "c.avi");
    EXPECT_EQ(sorted[1].name(), "b.jpg");
    EXPECT_EQ(sorted[2].name(), "a.txt");
}

TEST(FileNavigationUseCase, SortByFileTypeIsCaseInsensitive)
{
    std::vector<FileNode> files{
        makeFile("C:/data/a.TXT", 1),
        makeFile("C:/data/b.jpg", 2),
    };

    auto sorted = FileNavigationUseCase::sortBy(files, SortCriterion::FileType, true);

    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0].name(), "b.jpg");
    EXPECT_EQ(sorted[1].name(), "a.TXT");
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

TEST(FileNavigationUseCase, FilterByExtensionMatchesFilesWithNonAsciiNameWithoutThrowing)
{
    std::vector<FileNode> files{ makeFile(std::filesystem::path(std::u8string(u8"Café report.txt")), 1),
                                  makeFile("C:/data/b.jpg", 2) };

    std::vector<FileNode> filtered;
    EXPECT_NO_THROW(filtered = FileNavigationUseCase::filterByExtension(files, ".txt"));

    ASSERT_EQ(filtered.size(), 1u);
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

TEST(FileNavigationUseCase, FilterByNameMatchesNonAsciiCharactersWithoutThrowing)
{
    std::vector<FileNode> files{ makeFile(std::filesystem::path(std::u8string(u8"Café report.txt")), 1),
                                  makeFile("C:/data/b.jpg", 2) };

    std::vector<FileNode> filtered;
    EXPECT_NO_THROW(filtered = FileNavigationUseCase::filterByName(files, "report"));

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), std::filesystem::path(std::u8string(u8"Café report.txt")));
}

TEST(FileNavigationUseCase, FilterByNameReturnsEmptyWhenNoMatches)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "nomatch");

    EXPECT_TRUE(filtered.empty());
}

TEST(FileNavigationUseCase, FilterByNameStarSuffixMatchesByExtension)
{
    std::vector<FileNode> files{ makeFile("C:/data/report.pdf", 1), makeFile("C:/data/report.txt", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "*.pdf");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "report.pdf");
}

TEST(FileNavigationUseCase, FilterByNameStarPrefixMatchesByLeadingText)
{
    std::vector<FileNode> files{ makeFile("C:/data/doc1.txt", 1), makeFile("C:/data/report.txt", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "doc*");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "doc1.txt");
}

TEST(FileNavigationUseCase, FilterByNameQuestionMarkMatchesExactlyOneCharacter)
{
    std::vector<FileNode> files{
        makeFile("C:/data/file1.txt", 1),
        makeFile("C:/data/file12.txt", 2),
    };

    auto filtered = FileNavigationUseCase::filterByName(files, "file?.txt");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "file1.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsContainsModeDefaultsMatchOriginalOverload)
{
    std::vector<FileNode> files{ makeFile("C:/data/Report.txt", 1), makeFile("C:/data/b.jpg", 2) };

    auto filtered = FileNavigationUseCase::filterByName(files, "REPORT", NameSearchOptions{});

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "Report.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsContainsModeCaseSensitive)
{
    std::vector<FileNode> files{ makeFile("C:/data/Report.txt", 1), makeFile("C:/data/report.txt", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::Contains;
    options.caseSensitive = true;
    auto filtered = FileNavigationUseCase::filterByName(files, "Report", options);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "Report.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsWholeWordMode)
{
    std::vector<FileNode> files{ makeFile("C:/data/File Name.txt", 1), makeFile("C:/data/FileName.txt", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::WholeWord;
    auto filtered = FileNavigationUseCase::filterByName(files, "File", options);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "File Name.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsExactNameMode)
{
    std::vector<FileNode> files{ makeFile("C:/data/File.txt", 1), makeFile("C:/data/File.txt.bak", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::ExactName;
    auto filtered = FileNavigationUseCase::filterByName(files, "file.txt", options);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "File.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsExactNameModeCaseSensitive)
{
    std::vector<FileNode> files{ makeFile("C:/data/File.txt", 1), makeFile("C:/data/file.txt", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::ExactName;
    options.caseSensitive = true;
    auto filtered = FileNavigationUseCase::filterByName(files, "File.txt", options);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "File.txt");
}

TEST(FileNavigationUseCase, FilterByNameOptionsRegexMode)
{
    std::vector<FileNode> files{ makeFile("C:/data/Img123.png", 1), makeFile("C:/data/Vacation.png", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::Regex;
    auto filtered = FileNavigationUseCase::filterByName(files, "^Img\\d+", options);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name(), "Img123.png");
}

TEST(FileNavigationUseCase, FilterByNameOptionsRegexModeInvalidPatternMatchesNothing)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/b.txt", 2) };

    NameSearchOptions options;
    options.mode = NameMatchMode::Regex;

    std::vector<FileNode> filtered;
    EXPECT_NO_THROW(filtered = FileNavigationUseCase::filterByName(files, "[unterminated", options));
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

TEST(FileNavigationUseCase, FilterByCriteriaComposesBothFilters)
{
    std::vector<FileNode> files{
        makeFile("C:/data/report.jpg", 50),
        makeFile("C:/data/report.png", 5),
        makeFile("C:/data/report.txt", 50),
        makeFile("C:/data/other.jpg", 50),
    };

    SearchCriteria criteria;
    criteria.nameQuery = "report.*";
    criteria.minSizeBytes = 10;

    auto filtered = FileNavigationUseCase::filterByCriteria(files, criteria);

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].name(), "report.jpg");
    EXPECT_EQ(filtered[1].name(), "report.txt");
}

TEST(FileNavigationUseCase, FilterByCriteriaAllUnsetIsPassthrough)
{
    std::vector<FileNode> files{ makeFile("C:/data/a.txt", 1), makeFile("C:/data/folder", 0, FileType::Directory) };

    SearchCriteria criteria;
    auto filtered = FileNavigationUseCase::filterByCriteria(files, criteria);

    EXPECT_EQ(filtered.size(), 2u);
}

// Regression: advanced search (TabViewModel::runAdvancedSearch -> filterByCriteria) must surface
// symlinks. A symlink is reported with size 0 (StandardFileSystemRepository doesn't follow it to
// the target's size), so it must not be treated like a zero-byte file some filter excludes.
TEST(FileNavigationUseCase, FilterByCriteriaIncludesSymlinks)
{
    std::vector<FileNode> files{
        makeFile("C:/data/report.txt", 50),
        makeFile("C:/data/link.txt", 0, FileType::Symlink),
    };

    SearchCriteria criteria;
    criteria.nameQuery = "link";

    auto filtered = FileNavigationUseCase::filterByCriteria(files, criteria);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].fileType(), FileType::Symlink);
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

TEST(FileNavigationUseCase, ExtractArchiveDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    EXPECT_CALL(repository, extractArchive(std::filesystem::path("C:/data/photos.zip"), std::filesystem::path("C:/data/out")))
        .WillOnce(Return(Result<void>::success()));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.extractArchive("C:/data/photos.zip", "C:/data/out");

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

TEST(FileNavigationUseCase, DriveSpaceInfoDelegatesToRepository)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;
    DriveSpaceInfo info{ "Local Disk (C:)", 1000, 2000 };

    EXPECT_CALL(repository, driveSpaceInfo(std::filesystem::path("C:/data")))
        .WillOnce(Return(Result<DriveSpaceInfo>::success(info)));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.driveSpaceInfo("C:/data");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().displayLabel, "Local Disk (C:)");
    EXPECT_EQ(result.value().freeBytes, 1000u);
    EXPECT_EQ(result.value().totalBytes, 2000u);
}

TEST(FileNavigationUseCase, DriveSpaceInfoPropagatesError)
{
    MockFileSystemRepository repository;
    MockContextMenuProvider contextMenuProvider;

    EXPECT_CALL(repository, driveSpaceInfo(_))
        .WillOnce(Return(Result<DriveSpaceInfo>::failure(Error(ErrorCode::NotFound, "no drive"))));

    FileNavigationUseCase useCase(repository, contextMenuProvider);
    auto result = useCase.driveSpaceInfo("this-pc:");

    EXPECT_TRUE(result.hasError());
}
