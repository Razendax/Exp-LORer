#include <gtest/gtest.h>

#include <algorithm>

#include "ContentSearchUseCase.h"
#include "FileNavigationUseCase.h"
#include "MockContextMenuProvider.h"
#include "MockFileSystemRepository.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    FileNode makeFile(const std::filesystem::path& path, bool isHidden = false)
    {
        return FileNode::create(path, 10, std::chrono::system_clock::now(), std::chrono::system_clock::now(), FileType::Regular,
                                 isHidden)
            .value();
    }

    std::vector<std::byte> toBytes(const std::string& text)
    {
        std::vector<std::byte> bytes(text.size());
        std::transform(text.begin(), text.end(), bytes.begin(), [](char c) { return std::byte(c); });
        return bytes;
    }

    RawFileLines makeRawFile(const std::filesystem::path& path, std::vector<std::string> lines)
    {
        return RawFileLines{ makeFile(path), std::move(lines) };
    }
}

// --- textMatches ---------------------------------------------------------

TEST(ContentSearchUseCase, TextMatchesContainsModeIsCaseInsensitiveByDefault)
{
    EXPECT_TRUE(ContentSearchUseCase::textMatches("Hello World", "hello", TextMatchOptions{ TextMatchMode::Contains, false }));
    EXPECT_FALSE(ContentSearchUseCase::textMatches("Hello World", "hello", TextMatchOptions{ TextMatchMode::Contains, true }));
}

TEST(ContentSearchUseCase, TextMatchesWholeWordMode)
{
    EXPECT_TRUE(ContentSearchUseCase::textMatches("call foo(bar)", "foo", TextMatchOptions{ TextMatchMode::WholeWord, false }));
    EXPECT_FALSE(ContentSearchUseCase::textMatches("call foobar(bar)", "foo", TextMatchOptions{ TextMatchMode::WholeWord, false }));
}

TEST(ContentSearchUseCase, TextMatchesRegexMode)
{
    EXPECT_TRUE(ContentSearchUseCase::textMatches("value = 42", R"(\d+)", TextMatchOptions{ TextMatchMode::Regex, false }));
    EXPECT_FALSE(ContentSearchUseCase::textMatches("value = abc", R"(\d+)", TextMatchOptions{ TextMatchMode::Regex, false }));
}

TEST(ContentSearchUseCase, TextMatchesTreatsInvalidRegexAsNoMatch)
{
    EXPECT_FALSE(ContentSearchUseCase::textMatches("value", "(unclosed", TextMatchOptions{ TextMatchMode::Regex, false }));
}

// --- pathMatches (Include/Exclude) ----------------------------------------

TEST(ContentSearchUseCase, PathMatchesContainsModeWithoutWildcardsIsSubstring)
{
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/Foo.cpp", ".cpp", TextMatchOptions{}));
    EXPECT_FALSE(ContentSearchUseCase::pathMatches("src/Foo.h", ".cpp", TextMatchOptions{}));
}

TEST(ContentSearchUseCase, PathMatchesContainsModeSupportsStarWildcard)
{
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/Foo.cpp", "*.cpp", TextMatchOptions{}));
    EXPECT_FALSE(ContentSearchUseCase::pathMatches("src/Foo.h", "*.cpp", TextMatchOptions{}));
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/tests/Foo.cpp", "src/*.cpp", TextMatchOptions{}));
}

TEST(ContentSearchUseCase, PathMatchesContainsModeSupportsQuestionMarkWildcard)
{
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/a.txt", "src/?.txt", TextMatchOptions{}));
    EXPECT_FALSE(ContentSearchUseCase::pathMatches("src/ab.txt", "src/?.txt", TextMatchOptions{}));
}

TEST(ContentSearchUseCase, PathMatchesWholeWordMode)
{
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/foo/bar.cpp", "foo", TextMatchOptions{ TextMatchMode::WholeWord, false }));
    EXPECT_FALSE(ContentSearchUseCase::pathMatches("src/foobar/baz.cpp", "foo", TextMatchOptions{ TextMatchMode::WholeWord, false }));
}

TEST(ContentSearchUseCase, PathMatchesRegexMode)
{
    EXPECT_TRUE(ContentSearchUseCase::pathMatches("src/Foo123.cpp", R"(\d+)", TextMatchOptions{ TextMatchMode::Regex, false }));
    EXPECT_FALSE(ContentSearchUseCase::pathMatches("src/Foo.cpp", R"(\d+)", TextMatchOptions{ TextMatchMode::Regex, false }));
}

// --- filterLines ----------------------------------------------------------

TEST(ContentSearchUseCase, FilterLinesEmptyQueryReturnsNoMatches)
{
    std::vector<RawFileLines> files{ makeRawFile("C:/data/a.txt", { "hello" }) };

    auto matches = ContentSearchUseCase::filterLines(files, "", TextMatchOptions{});

    EXPECT_TRUE(matches.empty());
}

TEST(ContentSearchUseCase, FilterLinesKeepsOnlyMatchingLinesPerFile)
{
    std::vector<RawFileLines> files{
        makeRawFile("C:/data/a.txt", { "needle here", "nothing", "another needle" }),
        makeRawFile("C:/data/b.txt", { "nothing at all" }),
    };

    auto matches = ContentSearchUseCase::filterLines(files, "needle", TextMatchOptions{});

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].node.path(), std::filesystem::path("C:/data/a.txt"));
    ASSERT_EQ(matches[0].lines.size(), 2u);
    EXPECT_EQ(matches[0].lines[0].lineNumber, 1);
    EXPECT_EQ(matches[0].lines[0].textUtf8, "needle here");
    EXPECT_EQ(matches[0].lines[1].lineNumber, 3);
}

TEST(ContentSearchUseCase, FilterLinesRespectsCaseSensitivity)
{
    std::vector<RawFileLines> files{ makeRawFile("C:/data/a.txt", { "Needle" }) };

    auto caseSensitive = ContentSearchUseCase::filterLines(files, "needle", TextMatchOptions{ TextMatchMode::Contains, true });
    auto caseInsensitive = ContentSearchUseCase::filterLines(files, "needle", TextMatchOptions{ TextMatchMode::Contains, false });

    EXPECT_TRUE(caseSensitive.empty());
    ASSERT_EQ(caseInsensitive.size(), 1u);
}

TEST(ContentSearchUseCase, FilterLinesRegexMode)
{
    std::vector<RawFileLines> files{ makeRawFile("C:/data/a.txt", { "retries = 3", "no digits here" }) };

    auto matches = ContentSearchUseCase::filterLines(files, R"(\d+)", TextMatchOptions{ TextMatchMode::Regex, false });

    ASSERT_EQ(matches.size(), 1u);
    ASSERT_EQ(matches[0].lines.size(), 1u);
    EXPECT_EQ(matches[0].lines[0].lineNumber, 1);
}

// --- collectSearchableContent ---------------------------------------------

TEST(ContentSearchUseCase, CollectSearchableContentSplitsLinesAndSkipsBinaryAndHidden)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    std::vector<FileNode> listing{
        makeFile("C:/data/a.txt"),
        makeFile("C:/data/hidden.txt", /*isHidden=*/true),
        makeFile("C:/data/binary.bin"),
    };
    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(std::filesystem::path("C:/data"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));
    EXPECT_CALL(fileSystemRepository, readFilePrefix(std::filesystem::path("C:/data/a.txt"), ContentSearchUseCase::kMaxContentSearchBytes))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("line one\r\nline two"))));
    EXPECT_CALL(fileSystemRepository,
                readFilePrefix(std::filesystem::path("C:/data/binary.bin"), ContentSearchUseCase::kMaxContentSearchBytes))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes(std::string("bin\0ary", 7)))));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/false, "", TextMatchOptions{}, "",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].node.path(), std::filesystem::path("C:/data/a.txt"));
    ASSERT_EQ(result.value()[0].lines.size(), 2u);
    EXPECT_EQ(result.value()[0].lines[0], "line one");
    EXPECT_EQ(result.value()[0].lines[1], "line two");
}

TEST(ContentSearchUseCase, CollectSearchableContentShowHiddenIncludesHiddenFiles)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    std::vector<FileNode> listing{ makeFile("C:/data/hidden.txt", /*isHidden=*/true) };
    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(std::filesystem::path("C:/data"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));
    EXPECT_CALL(fileSystemRepository, readFilePrefix(std::filesystem::path("C:/data/hidden.txt"), _))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("secret"))));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/true, "", TextMatchOptions{}, "",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
}

TEST(ContentSearchUseCase, CollectSearchableContentAppliesIncludeAndExcludePatterns)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    std::vector<FileNode> listing{
        makeFile("C:/data/src/keep.cpp"),
        makeFile("C:/data/src/skip.h"),
        makeFile("C:/data/tests/keep_test.cpp"),
    };
    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(std::filesystem::path("C:/data"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));
    EXPECT_CALL(fileSystemRepository, readFilePrefix(std::filesystem::path("C:/data/src/keep.cpp"), _))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("content"))));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/false, ".cpp", TextMatchOptions{}, "tests",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].node.path(), std::filesystem::path("C:/data/src/keep.cpp"));
}

TEST(ContentSearchUseCase, CollectSearchableContentIncludePatternSupportsWildcards)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    std::vector<FileNode> listing{
        makeFile("C:/data/src/keep.cpp"),
        makeFile("C:/data/src/skip.h"),
    };
    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(std::filesystem::path("C:/data"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));
    EXPECT_CALL(fileSystemRepository, readFilePrefix(std::filesystem::path("C:/data/src/keep.cpp"), _))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(toBytes("content"))));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/false, "*.cpp", TextMatchOptions{}, "",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].node.path(), std::filesystem::path("C:/data/src/keep.cpp"));
}

TEST(ContentSearchUseCase, CollectSearchableContentSkipsArchives)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    std::vector<FileNode> listing{ makeFile("C:/data/archive.zip") };
    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(std::filesystem::path("C:/data"), _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::success(listing)));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/false, "", TextMatchOptions{}, "",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().empty());
}

TEST(ContentSearchUseCase, CollectSearchableContentPropagatesListingFailure)
{
    MockFileSystemRepository fileSystemRepository;
    MockContextMenuProvider contextMenuProvider;
    FileNavigationUseCase fileNavigationUseCase(fileSystemRepository, contextMenuProvider);
    ContentSearchUseCase useCase(fileNavigationUseCase, fileSystemRepository);

    EXPECT_CALL(fileSystemRepository, listDirectoryRecursive(_, _, _))
        .WillOnce(Return(Result<std::vector<FileNode>>::failure(Error(ErrorCode::NotFound, "gone"))));

    auto result = useCase.collectSearchableContent("C:/data", nullptr, /*showHidden=*/false, "", TextMatchOptions{}, "",
                                                      TextMatchOptions{});

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}
