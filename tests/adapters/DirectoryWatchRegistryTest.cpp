#include <gtest/gtest.h>

#include "DirectoryWatchRegistry.h"

TEST(DirectoryWatchRegistry, FirstWatchOnPathSignalsStart)
{
    DirectoryWatchRegistry registry;

    const auto outcome = registry.addWatch(L"C:/repo", []() {});

    EXPECT_TRUE(outcome.shouldStartOsWatch);
}

TEST(DirectoryWatchRegistry, SecondWatchOnSamePathSignalsAlreadyWatching)
{
    DirectoryWatchRegistry registry;

    registry.addWatch(L"C:/repo", []() {});
    const auto second = registry.addWatch(L"C:/repo", []() {});

    EXPECT_FALSE(second.shouldStartOsWatch);
}

TEST(DirectoryWatchRegistry, UnwatchWithOtherWatchersRemainingKeepsWatching)
{
    DirectoryWatchRegistry registry;

    const auto first = registry.addWatch(L"C:/repo", []() {});
    registry.addWatch(L"C:/repo", []() {});

    const auto removeOutcome = registry.removeWatch(first.id);

    EXPECT_TRUE(removeOutcome.found);
    EXPECT_FALSE(removeOutcome.shouldStopOsWatch);
}

TEST(DirectoryWatchRegistry, UnwatchOfLastWatcherSignalsStop)
{
    DirectoryWatchRegistry registry;

    const auto outcome = registry.addWatch(L"C:/repo", []() {});
    const auto removeOutcome = registry.removeWatch(outcome.id);

    EXPECT_TRUE(removeOutcome.found);
    EXPECT_TRUE(removeOutcome.shouldStopOsWatch);
    EXPECT_EQ(removeOutcome.pathKey, L"C:/repo");
}

TEST(DirectoryWatchRegistry, CallbacksForPathReturnsRightSet)
{
    DirectoryWatchRegistry registry;

    int callsA = 0;
    int callsB = 0;
    registry.addWatch(L"C:/repo", [&callsA]() { ++callsA; });
    registry.addWatch(L"C:/repo", [&callsB]() { ++callsB; });
    registry.addWatch(L"C:/other", [&callsA]() { ++callsA; });

    const auto callbacks = registry.callbacksForPath(L"C:/repo");
    ASSERT_EQ(callbacks.size(), 2u);
    for (const auto& callback : callbacks)
    {
        callback();
    }

    EXPECT_EQ(callsA, 1);
    EXPECT_EQ(callsB, 1);
}

TEST(DirectoryWatchRegistry, UnwatchingUnknownIdIsNoOp)
{
    DirectoryWatchRegistry registry;

    const auto removeOutcome = registry.removeWatch(999);

    EXPECT_FALSE(removeOutcome.found);
}
