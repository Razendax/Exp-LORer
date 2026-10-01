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

TEST(PathContainment, RebasePathReplacesPrefixKeepingTail)
{
    EXPECT_EQ(rebasePath(std::filesystem::path("C:/repo/src/domain"), std::filesystem::path("C:/repo"),
                         std::filesystem::path("C:/renamed")),
              std::filesystem::path("C:/renamed/src/domain"));
}

TEST(PathContainment, RebasePathOfRootItselfReturnsNewRoot)
{
    EXPECT_EQ(rebasePath(std::filesystem::path("C:/repo"), std::filesystem::path("C:/repo"), std::filesystem::path("C:/renamed")),
              std::filesystem::path("C:/renamed"));
}

TEST(PathContainment, RebasePathIsCaseInsensitiveOnThePrefix)
{
    EXPECT_EQ(rebasePath(std::filesystem::path("C:/Repo/Src"), std::filesystem::path("c:/repo/"), std::filesystem::path("C:/x")),
              std::filesystem::path("C:/x/Src"));
}

TEST(PathContainment, RebasePathLeavesUnrelatedPathUnchanged)
{
    EXPECT_EQ(rebasePath(std::filesystem::path("D:/other"), std::filesystem::path("C:/repo"), std::filesystem::path("C:/x")),
              std::filesystem::path("D:/other"));
}

TEST(PathContainment, RebasePathKeepsNonAsciiComponents)
{
    const std::filesystem::path root(u8"C:/Папка");
    const std::filesystem::path candidate(u8"C:/Папка/日本語/😀");
    EXPECT_EQ(rebasePath(candidate, root, std::filesystem::path(u8"C:/Новая")), std::filesystem::path(u8"C:/Новая/日本語/😀"));
}
