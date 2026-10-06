#include <atomic>

#include <gtest/gtest.h>

#include "DiskUsageUseCase.h"
#include "MockDiskUsageRepository.h"
#include "MockDiskUsageScanner.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    std::vector<DiskUsageNode> sampleNodes()
    {
        return {
            DiskUsageNode{ std::filesystem::path("C:/repo"), "repo", 300, true },
            DiskUsageNode{ std::filesystem::path("C:/repo/a"), "a", 200, true },
            DiskUsageNode{ std::filesystem::path("C:/repo/b.txt"), "b.txt", 100, false },
        };
    }

    std::atomic<bool> kNotCancelled{ false };
}

TEST(DiskUsageUseCase, ScanAndPersistPersistsScanResultAndReturnsIt)
{
    MockDiskUsageScanner scanner;
    MockDiskUsageRepository repository;

    const std::vector<DiskUsageNode> nodes = sampleNodes();
    EXPECT_CALL(scanner, scan(std::filesystem::path("C:/repo"), _, _))
        .WillOnce(Return(Result<std::vector<DiskUsageNode>>::success(nodes)));
    EXPECT_CALL(repository, saveScan(std::filesystem::path("C:/repo"), nodes)).WillOnce(Return(Result<void>::success()));

    DiskUsageUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 3u);
}

TEST(DiskUsageUseCase, ScanAndPersistPropagatesScanFailureWithoutSaving)
{
    MockDiskUsageScanner scanner;
    MockDiskUsageRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _))
        .WillOnce(Return(Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::NotFound, "missing"))));
    EXPECT_CALL(repository, saveScan(_, _)).Times(0);

    DiskUsageUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/missing"), nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(DiskUsageUseCase, ScanAndPersistDoesNotSaveOnCancellation)
{
    MockDiskUsageScanner scanner;
    MockDiskUsageRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _))
        .WillOnce(Return(Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::Cancelled, "cancelled"))));
    EXPECT_CALL(repository, saveScan(_, _)).Times(0);

    DiskUsageUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(DiskUsageUseCase, ScanAndPersistPropagatesSaveFailure)
{
    MockDiskUsageScanner scanner;
    MockDiskUsageRepository repository;

    EXPECT_CALL(scanner, scan(_, _, _)).WillOnce(Return(Result<std::vector<DiskUsageNode>>::success(sampleNodes())));
    EXPECT_CALL(repository, saveScan(_, _)).WillOnce(Return(Result<void>::failure(Error(ErrorCode::IoError, "disk full"))));

    DiskUsageUseCase useCase(scanner, repository);
    auto result = useCase.scanAndPersist(std::filesystem::path("C:/repo"), nullptr, kNotCancelled);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}

TEST(DiskUsageUseCase, TopChildrenSortsDescendingBySize)
{
    std::vector<DiskUsageNode> children = {
        DiskUsageNode{ std::filesystem::path("C:/repo/small"), "small", 10, false },
        DiskUsageNode{ std::filesystem::path("C:/repo/big"), "big", 300, true },
        DiskUsageNode{ std::filesystem::path("C:/repo/mid"), "mid", 100, false },
    };

    auto top = DiskUsageUseCase::topChildren(std::move(children), 10);

    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].name, "big");
    EXPECT_EQ(top[1].name, "mid");
    EXPECT_EQ(top[2].name, "small");
}

TEST(DiskUsageUseCase, TopChildrenTruncatesToMax)
{
    std::vector<DiskUsageNode> children;
    for (int i = 0; i < 15; ++i)
    {
        children.push_back(DiskUsageNode{ std::filesystem::path("C:/repo/" + std::to_string(i)), std::to_string(i),
                                           static_cast<std::uintmax_t>(i), false });
    }

    auto top = DiskUsageUseCase::topChildren(std::move(children), 10);

    ASSERT_EQ(top.size(), 10u);
    // Largest ("14") first.
    EXPECT_EQ(top.front().name, "14");
}

TEST(DiskUsageUseCase, ChildrenForDelegatesToRepository)
{
    MockDiskUsageScanner scanner;
    MockDiskUsageRepository repository;

    const std::vector<DiskUsageNode> children = { DiskUsageNode{ std::filesystem::path("C:/repo/a"), "a", 50, false } };
    EXPECT_CALL(repository, childrenOf(std::filesystem::path("C:/repo")))
        .WillOnce(Return(Result<std::vector<DiskUsageNode>>::success(children)));

    DiskUsageUseCase useCase(scanner, repository);
    auto result = useCase.childrenFor(std::filesystem::path("C:/repo"));

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].name, "a");
}
