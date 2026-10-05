#include <gtest/gtest.h>

#include <atomic>

#include "DuplicateFinderUseCase.h"
#include "MockDuplicateFileScanner.h"

using ::testing::_;
using ::testing::Return;

namespace
{
    DuplicateFileGroup makeGroup(std::uint64_t hash, std::uintmax_t size, std::vector<std::filesystem::path> paths)
    {
        return DuplicateFileGroup{ hash, size, std::move(paths) };
    }
}

TEST(DuplicateFinderUseCase, ScanDelegatesToScanner)
{
    MockDuplicateFileScanner scanner;

    const std::vector<DuplicateFileGroup> groups = {
        makeGroup(1, 100, { "C:/a.txt", "C:/b.txt" }),
    };
    EXPECT_CALL(scanner, scan(std::filesystem::path("C:/repo"), 4096u, _, _))
        .WillOnce(Return(Result<std::vector<DuplicateFileGroup>>::success(groups)));

    DuplicateFinderUseCase useCase(scanner);
    std::atomic<bool> cancelFlag{ false };
    auto result = useCase.scan(std::filesystem::path("C:/repo"), 4096u, nullptr, cancelFlag);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST(DuplicateFinderUseCase, ScanPropagatesScannerFailure)
{
    MockDuplicateFileScanner scanner;

    EXPECT_CALL(scanner, scan(_, _, _, _)).WillOnce(Return(Result<std::vector<DuplicateFileGroup>>::failure(Error(ErrorCode::Cancelled, "cancelled"))));

    DuplicateFinderUseCase useCase(scanner);
    std::atomic<bool> cancelFlag{ false };
    auto result = useCase.scan(std::filesystem::path("C:/repo"), 0u, nullptr, cancelFlag);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(DuplicateFinderUseCase, TotalWastedBytesSumsAcrossGroups)
{
    std::vector<DuplicateFileGroup> groups = {
        makeGroup(1, 100, { "C:/a1.txt", "C:/a2.txt" }),       // 1 extra copy * 100 = 100
        makeGroup(2, 50, { "C:/b1.txt", "C:/b2.txt", "C:/b3.txt" }), // 2 extra copies * 50 = 100
    };

    EXPECT_EQ(DuplicateFinderUseCase::totalWastedBytes(groups), 200u);
}

TEST(DuplicateFinderUseCase, TotalWastedBytesIgnoresMalformedSingleFileGroup)
{
    std::vector<DuplicateFileGroup> groups = { makeGroup(1, 100, { "C:/a1.txt" }) };

    EXPECT_EQ(DuplicateFinderUseCase::totalWastedBytes(groups), 0u);
}

TEST(DuplicateFinderUseCase, GroupsSortedByWasteDescendingOrder)
{
    std::vector<DuplicateFileGroup> groups = {
        makeGroup(1, 10, { "C:/small1.txt", "C:/small2.txt" }),                                // waste 10
        makeGroup(2, 1000, { "C:/big1.txt", "C:/big2.txt" }),                                   // waste 1000
        makeGroup(3, 50, { "C:/mid1.txt", "C:/mid2.txt", "C:/mid3.txt" }),                       // waste 100
    };

    auto sorted = DuplicateFinderUseCase::groupsSortedByWaste(std::move(groups));

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].contentHash, 2u);
    EXPECT_EQ(sorted[1].contentHash, 3u);
    EXPECT_EQ(sorted[2].contentHash, 1u);
}
