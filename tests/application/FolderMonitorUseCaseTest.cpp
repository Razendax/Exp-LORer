#include <optional>

#include <gtest/gtest.h>

#include "FolderMonitorUseCase.h"
#include "MockFolderMonitor.h"

using ::testing::_;
using ::testing::Return;

TEST(FolderMonitorUseCase, StartMonitoringDelegatesToMonitorAndPassesThroughSuccess)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, startMonitoring(std::filesystem::path("C:/repo"), ::testing::_, ::testing::_))
        .WillOnce([](const std::filesystem::path&, std::function<void(const std::vector<FolderChangeEvent>&)>,
                     std::function<void(Result<FolderMonitorId>)> onStarted) { onStarted(Result<FolderMonitorId>::success(42)); });

    FolderMonitorUseCase useCase(monitor);
    std::optional<Result<FolderMonitorId>> reported;
    useCase.startMonitoring(
        std::filesystem::path("C:/repo"), [](const std::vector<FolderChangeEvent>&) {},
        [&reported](Result<FolderMonitorId> result) { reported = std::move(result); });

    ASSERT_TRUE(reported.has_value());
    ASSERT_TRUE(reported->hasValue());
    EXPECT_EQ(reported->value(), 42u);
}

TEST(FolderMonitorUseCase, StartMonitoringDelegatesToMonitorAndPassesThroughFailure)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, startMonitoring(_, _, _))
        .WillOnce([](const std::filesystem::path&, std::function<void(const std::vector<FolderChangeEvent>&)>,
                     std::function<void(Result<FolderMonitorId>)> onStarted) {
            onStarted(Result<FolderMonitorId>::failure(Error(ErrorCode::InvalidArgument, "bad path")));
        });

    FolderMonitorUseCase useCase(monitor);
    std::optional<Result<FolderMonitorId>> reported;
    useCase.startMonitoring(
        std::filesystem::path("this-pc:"), [](const std::vector<FolderChangeEvent>&) {},
        [&reported](Result<FolderMonitorId> result) { reported = std::move(result); });

    ASSERT_TRUE(reported.has_value());
    ASSERT_TRUE(reported->hasError());
    EXPECT_EQ(reported->error().code, ErrorCode::InvalidArgument);
}

TEST(FolderMonitorUseCase, StopMonitoringDelegatesToMonitor)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, stopMonitoring(FolderMonitorId{ 7 })).Times(1);

    FolderMonitorUseCase useCase(monitor);
    useCase.stopMonitoring(7);
}
