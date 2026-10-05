#include <atomic>

#include <gtest/gtest.h>

#include "FragmentationUseCase.h"
#include "MockFragmentationRepository.h"
#include "MockFragmentationScanner.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    std::vector<FileFragmentationInfo> sampleFiles()
    {
        return {
            FileFragmentationInfo{ std::filesystem::path("C:/repo/a.txt"), 100, 1 },
            FileFragmentationInfo{ std::filesystem::path("C:/repo/b.txt"), 200, 5 },
        };
    }

    std::atomic<bool> kNotCancelled{ false };
}

TEST(FragmentationUseCase, ScanAndPersistPersistsScanResultAndReturnsIt)
{
    MockFragmentationScanner scanner;
    MockFragmentationRepository repository;

    const std::vector<FileFragmentationInfo> files = sampleFiles();
    EXPECT_CALL(scanner, scan(std::filesystem::path("C:/repo"), 1024u, _, _))
        .WillOnce(Return(Result<std::vector<FileFragmentationInfo>>::success(files)));
    EXPECT_CALL(repository, saveScan(std::filesystem::path("C:/repo"), 1024u, files)).WillOnce(Return(Result<void>::success()));

    FragmentationUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), 1024u, nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u);
}

TEST(FragmentationUseCase, ScanAndPersistPropagatesScanFailureWithoutSaving)
{
    MockFragmentationScanner scanner;
    MockFragmentationRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _, _))
        .WillOnce(Return(Result<std::vector<FileFragmentationInfo>>::failure(Error(ErrorCode::NotFound, "missing"))));
    EXPECT_CALL(repository, saveScan(_, _, _)).Times(0);

    FragmentationUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/missing"), 1024u, nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(FragmentationUseCase, ScanAndPersistDoesNotSaveOnCancellation)
{
    MockFragmentationScanner scanner;
    MockFragmentationRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _, _))
        .WillOnce(Return(Result<std::vector<FileFragmentationInfo>>::failure(Error(ErrorCode::Cancelled, "cancelled"))));
    EXPECT_CALL(repository, saveScan(_, _, _)).Times(0);

    FragmentationUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), 1024u, nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(FragmentationUseCase, ScanAndPersistPropagatesSaveFailure)
{
    MockFragmentationScanner scanner;
    MockFragmentationRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _, _)).WillOnce(Return(Result<std::vector<FileFragmentationInfo>>::success(sampleFiles())));
    EXPECT_CALL(repository, saveScan(_, _, _)).WillOnce(Return(Result<void>::failure(Error(ErrorCode::IoError, "disk full"))));

    FragmentationUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), 1024u, nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(FragmentationUseCase, FilesUnderDelegatesToRepository)
{
    MockFragmentationScanner scanner;
    MockFragmentationRepository repository;

    const std::vector<FileFragmentationInfo> files = { FileFragmentationInfo{ std::filesystem::path("C:/repo/a.txt"), 50, 1 } };
    EXPECT_CALL(repository, filesUnder(std::filesystem::path("C:/repo")))
        .WillOnce(Return(Result<std::vector<FileFragmentationInfo>>::success(files)));

    FragmentationUseCase useCase(scanner, repository);
    auto result = useCase.filesUnder(std::filesystem::path("C:/repo"));

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].path, std::filesystem::path("C:/repo/a.txt"));
}

TEST(FragmentationUseCase, SummarizeEmptyList)
{
    const FragmentationSummary summary = FragmentationUseCase::summarize({});

    EXPECT_EQ(summary.filesScanned, 0u);
    EXPECT_EQ(summary.totalBytesScanned, 0u);
    EXPECT_EQ(summary.fragmentedFileCount, 0u);
    EXPECT_EQ(summary.totalFragments, 0u);
}

TEST(FragmentationUseCase, SummarizeAllContiguous)
{
    const std::vector<FileFragmentationInfo> files = {
        FileFragmentationInfo{ std::filesystem::path("C:/repo/a.txt"), 100, 1 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/b.txt"), 200, 1 },
    };

    const FragmentationSummary summary = FragmentationUseCase::summarize(files);

    EXPECT_EQ(summary.filesScanned, 2u);
    EXPECT_EQ(summary.totalBytesScanned, 300u);
    EXPECT_EQ(summary.fragmentedFileCount, 0u);
    EXPECT_EQ(summary.totalFragments, 2u);
}

TEST(FragmentationUseCase, SummarizeMixed)
{
    const std::vector<FileFragmentationInfo> files = {
        FileFragmentationInfo{ std::filesystem::path("C:/repo/a.txt"), 100, 1 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/b.txt"), 200, 5 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/c.txt"), 300, 3 },
    };

    const FragmentationSummary summary = FragmentationUseCase::summarize(files);

    EXPECT_EQ(summary.filesScanned, 3u);
    EXPECT_EQ(summary.totalBytesScanned, 600u);
    EXPECT_EQ(summary.fragmentedFileCount, 2u);
    EXPECT_EQ(summary.totalFragments, 9u);
}

TEST(FragmentationUseCase, SortedByFragmentCountDescendingWithTie)
{
    std::vector<FileFragmentationInfo> files = {
        FileFragmentationInfo{ std::filesystem::path("C:/repo/a.txt"), 100, 2 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/b.txt"), 200, 9 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/c.txt"), 300, 9 },
        FileFragmentationInfo{ std::filesystem::path("C:/repo/d.txt"), 400, 1 },
    };

    auto sorted = FragmentationUseCase::sortedByFragmentCount(std::move(files));

    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0].fragmentCount, 9u);
    EXPECT_EQ(sorted[1].fragmentCount, 9u);
    EXPECT_EQ(sorted[2].fragmentCount, 2u);
    EXPECT_EQ(sorted[3].fragmentCount, 1u);
}
