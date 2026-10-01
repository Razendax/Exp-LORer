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

TEST(NavigationHistory, PeekBackReturnsSameValueAsGoBackWithoutMoving)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");

    auto peeked = history.peekBack();

    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, std::filesystem::path("C:/a"));
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/b"));
    EXPECT_TRUE(history.canGoBack());
    EXPECT_FALSE(history.canGoForward());
}

TEST(NavigationHistory, PeekForwardReturnsSameValueAsGoForwardWithoutMoving)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");
    history.goBack();

    auto peeked = history.peekForward();

    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, std::filesystem::path("C:/b"));
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/a"));
    EXPECT_TRUE(history.canGoForward());
}

TEST(NavigationHistory, PeekBackOnEmptyHistoryReturnsNullopt)
{
    NavigationHistory history;

    EXPECT_FALSE(history.peekBack().has_value());
}

TEST(NavigationHistory, PeekForwardOnEmptyForwardStackReturnsNullopt)
{
    NavigationHistory history;
    history.navigate("C:/a");

    EXPECT_FALSE(history.peekForward().has_value());
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

TEST(NavigationHistory, ReplaceCurrentSwapsPathWithoutTouchingBackOrForward)
{
    NavigationHistory history;
    history.navigate("C:/a");
    history.navigate("C:/b");
    history.navigate("C:/c");
    history.goBack();

    history.replaceCurrent("C:/renamed");

    ASSERT_TRUE(history.current().has_value());
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/renamed"));
    EXPECT_EQ(history.peekBack(), std::filesystem::path("C:/a"));
    EXPECT_EQ(history.peekForward(), std::filesystem::path("C:/c"));
}

TEST(NavigationHistory, ReplaceCurrentWithNoCurrentBehavesLikeNavigate)
{
    NavigationHistory history;

    history.replaceCurrent("C:/a");

    ASSERT_TRUE(history.current().has_value());
    EXPECT_EQ(*history.current(), std::filesystem::path("C:/a"));
    EXPECT_FALSE(history.canGoBack());
}
