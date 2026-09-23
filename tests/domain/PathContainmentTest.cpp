#include <gtest/gtest.h>

#include "PathContainment.h"

TEST(PathContainment, RootIsWithinItself)
{
    EXPECT_TRUE(isPathWithinOrEqual(std::filesystem::path("C:/repo"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, DirectChildIsWithin)
{
    EXPECT_TRUE(isPathWithinOrEqual(std::filesystem::path("C:/repo/src"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, GrandchildIsWithin)
{
    EXPECT_TRUE(isPathWithinOrEqual(std::filesystem::path("C:/repo/src/domain"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, SiblingIsNotWithin)
{
    EXPECT_FALSE(isPathWithinOrEqual(std::filesystem::path("C:/repo-other"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, ParentIsNotWithin)
{
    EXPECT_FALSE(isPathWithinOrEqual(std::filesystem::path("C:/"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, UnrelatedDriveIsNotWithin)
{
    EXPECT_FALSE(isPathWithinOrEqual(std::filesystem::path("D:/repo"), std::filesystem::path("C:/repo")));
}

TEST(PathContainment, EmptyCandidateIsNotWithinNonEmptyRoot)
{
    EXPECT_FALSE(isPathWithinOrEqual(std::filesystem::path(), std::filesystem::path("C:/repo")));
}
