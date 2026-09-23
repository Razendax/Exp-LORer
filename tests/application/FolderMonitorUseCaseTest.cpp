#include <gtest/gtest.h>

#include "FolderMonitorUseCase.h"
#include "MockFolderMonitor.h"

using ::testing::_;
using ::testing::Return;

TEST(FolderMonitorUseCase, StartMonitoringDelegatesToMonitorAndPassesThroughSuccess)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, startMonitoring(std::filesystem::path("C:/repo"), ::testing::_))
        .WillOnce(Return(Result<FolderMonitorId>::success(42)));

    FolderMonitorUseCase useCase(monitor);
    auto result = useCase.startMonitoring(std::filesystem::path("C:/repo"), [](const std::vector<FolderChangeEvent>&) {});

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42u);
}

TEST(FolderMonitorUseCase, StartMonitoringDelegatesToMonitorAndPassesThroughFailure)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, startMonitoring(_, _))
        .WillOnce(Return(Result<FolderMonitorId>::failure(Error(ErrorCode::InvalidArgument, "bad path"))));

    FolderMonitorUseCase useCase(monitor);
    auto result = useCase.startMonitoring(std::filesystem::path("this-pc:"), [](const std::vector<FolderChangeEvent>&) {});

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(FolderMonitorUseCase, StopMonitoringDelegatesToMonitor)
{
    MockFolderMonitor monitor;

    EXPECT_CALL(monitor, stopMonitoring(FolderMonitorId{ 7 })).Times(1);

    FolderMonitorUseCase useCase(monitor);
    useCase.stopMonitoring(7);
}
