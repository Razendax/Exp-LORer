#include <gtest/gtest.h>

#include "TagHierarchy.h"

TEST(TagHierarchy, SelfMatches)
{
    EXPECT_TRUE(TagHierarchy::isSelfOrDescendant("game", "game"));
}

TEST(TagHierarchy, DirectChildMatches)
{
    EXPECT_TRUE(TagHierarchy::isSelfOrDescendant("game/action", "game"));
}

TEST(TagHierarchy, MultiLevelDescendantMatches)
{
    EXPECT_TRUE(TagHierarchy::isSelfOrDescendant("game/action/rpg", "game"));
}

TEST(TagHierarchy, SiblingWithSharedPrefixDoesNotMatch)
{
    EXPECT_FALSE(TagHierarchy::isSelfOrDescendant("game2", "game"));
}

TEST(TagHierarchy, UnrelatedNameDoesNotMatch)
{
    EXPECT_FALSE(TagHierarchy::isSelfOrDescendant("work", "game"));
}

TEST(TagHierarchy, ParentIsNotDescendantOfChild)
{
    EXPECT_FALSE(TagHierarchy::isSelfOrDescendant("game", "game/action"));
}

TEST(TagHierarchy, ComparisonIsCaseInsensitive)
{
    EXPECT_TRUE(TagHierarchy::isSelfOrDescendant("Game/Action", "game"));
    EXPECT_TRUE(TagHierarchy::isSelfOrDescendant("game/action", "GAME"));
}
