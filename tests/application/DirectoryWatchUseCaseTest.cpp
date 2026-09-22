#include <gtest/gtest.h>

#include "DirectoryWatchUseCase.h"
#include "MockDirectoryWatcher.h"

using ::testing::_;
using ::testing::Return;

TEST(DirectoryWatchUseCase, WatchDelegatesToWatcherAndPassesThroughSuccess)
{
    MockDirectoryWatcher watcher;

    EXPECT_CALL(watcher, watch(std::filesystem::path("C:/repo"), ::testing::_))
        .WillOnce(Return(Result<DirectoryWatchId>::success(42)));

    DirectoryWatchUseCase useCase(watcher);
    auto result = useCase.watch(std::filesystem::path("C:/repo"), []() {});

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 42u);
}

TEST(DirectoryWatchUseCase, WatchDelegatesToWatcherAndPassesThroughFailure)
{
    MockDirectoryWatcher watcher;

    EXPECT_CALL(watcher, watch(_, _)).WillOnce(Return(Result<DirectoryWatchId>::failure(Error(ErrorCode::InvalidArgument, "bad path"))));

    DirectoryWatchUseCase useCase(watcher);
    auto result = useCase.watch(std::filesystem::path("this-pc:"), []() {});

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(DirectoryWatchUseCase, UnwatchDelegatesToWatcher)
{
    MockDirectoryWatcher watcher;

    EXPECT_CALL(watcher, unwatch(DirectoryWatchId{ 7 })).Times(1);

    DirectoryWatchUseCase useCase(watcher);
    useCase.unwatch(7);
}
