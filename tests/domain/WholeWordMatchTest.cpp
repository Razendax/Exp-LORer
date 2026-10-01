#include <gtest/gtest.h>

#include "WholeWordMatch.h"

TEST(WholeWordMatch, MatchesWholeWordSeparatedBySpace)
{
    EXPECT_TRUE(WholeWordMatch::matches("File", "File Name"));
}

TEST(WholeWordMatch, DoesNotMatchAsSubstringOfLongerWord)
{
    EXPECT_FALSE(WholeWordMatch::matches("File", "FileName"));
}

TEST(WholeWordMatch, MatchesWithPunctuationBoundary)
{
    EXPECT_TRUE(WholeWordMatch::matches("File", "File-Name"));
    EXPECT_TRUE(WholeWordMatch::matches("File", "File_Name"));
    EXPECT_TRUE(WholeWordMatch::matches("File", "File.txt"));
}

TEST(WholeWordMatch, CaseInsensitiveByDefault)
{
    EXPECT_TRUE(WholeWordMatch::matches("file", "File Name"));
}

TEST(WholeWordMatch, CaseSensitiveWhenRequested)
{
    EXPECT_FALSE(WholeWordMatch::matches("file", "File Name", true));
    EXPECT_TRUE(WholeWordMatch::matches("File", "File Name", true));
}

TEST(WholeWordMatch, EmptyQueryNeverMatches)
{
    EXPECT_FALSE(WholeWordMatch::matches("", "File Name"));
}
