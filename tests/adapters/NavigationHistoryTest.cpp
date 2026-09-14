#include <gtest/gtest.h>

#include "NavigationHistory.h"

TEST(NavigationHistory, StartsWithNoCurrentPath)
{
    NavigationHistory history;

    EXPECT_FALSE(history.current().has_value());
    EXPECT_FALSE(history.canGoBack());
    EXPECT_FALSE(history.canGoForward());
}

TEST(NavigationHistory, NavigateSetsCurrentAndDisablesForward)
{
    NavigationHistory history;

    history.navigate("C:/a");

    ASSERT_TRUE(history.current().has_value());
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/a"));
    EXPECT_FALSE(history.canGoBack());
    EXPECT_FALSE(history.canGoForward());
}

TEST(NavigationHistory, GoBackReturnsPreviousPathAndEnablesForward)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");

    auto back = history.goBack();

    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, std::filesystem::path("C:/a"));
    EXPECT_TRUE(history.canGoForward());
    EXPECT_FALSE(history.canGoBack());
}

TEST(NavigationHistory, GoForwardAfterGoBackReturnsToNextPath)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");
    history.goBack();

    auto forward = history.goForward();

    ASSERT_TRUE(forward.has_value());
    EXPECT_EQ(*forward, std::filesystem::path("C:/b"));
    EXPECT_FALSE(history.canGoForward());
    EXPECT_TRUE(history.canGoBack());
}

TEST(NavigationHistory, GoBackOnEmptyHistoryReturnsNullopt)
{
    NavigationHistory history;

    EXPECT_FALSE(history.goBack().has_value());
}

TEST(NavigationHistory, GoForwardOnEmptyForwardStackReturnsNullopt)
{
    NavigationHistory history;
    history.navigate("C:/a");

    EXPECT_FALSE(history.goForward().has_value());
}

TEST(NavigationHistory, NavigateAfterGoBackClearsForwardStack)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");
    history.goBack();

    history.navigate("C:/c");

    EXPECT_FALSE(history.canGoForward());
    EXPECT_TRUE(history.canGoBack());
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/c"));
}
