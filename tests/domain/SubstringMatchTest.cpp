#include <gtest/gtest.h>

#include "SubstringMatch.h"

TEST(SubstringMatch, MatchesSubstringAnywhere)
{
    EXPECT_TRUE(SubstringMatch::matches("ile", "File Name"));
}

TEST(SubstringMatch, DoesNotMatchWhenAbsent)
{
    EXPECT_FALSE(SubstringMatch::matches("xyz", "File Name"));
}

TEST(SubstringMatch, CaseInsensitiveByDefault)
{
    EXPECT_TRUE(SubstringMatch::matches("file", "File Name"));
}

TEST(SubstringMatch, CaseSensitiveWhenRequested)
{
    EXPECT_FALSE(SubstringMatch::matches("file", "File Name", true));
    EXPECT_TRUE(SubstringMatch::matches("File", "File Name", true));
}

TEST(SubstringMatch, EmptyQueryNeverMatches)
{
    EXPECT_FALSE(SubstringMatch::matches("", "File Name"));
}

TEST(SubstringMatch, WildcardCharactersAreLiteral)
{
    EXPECT_FALSE(SubstringMatch::matches("*", "File Name"));
    EXPECT_TRUE(SubstringMatch::matches("*", "a*b"));
}
