#include <gtest/gtest.h>

#include "GlobMatch.h"

TEST(GlobMatch, StarMatchesAnyRunIncludingNone)
{
    EXPECT_TRUE(GlobMatch::matches("*.exe", "setup.exe"));
    EXPECT_TRUE(GlobMatch::matches("*.exe", ".exe"));
    EXPECT_FALSE(GlobMatch::matches("*.exe", "setup.dll"));
}

TEST(GlobMatch, QuestionMarkMatchesExactlyOneCharacter)
{
    EXPECT_TRUE(GlobMatch::matches("Image_?.png", "Image_1.png"));
    EXPECT_TRUE(GlobMatch::matches("Image_?.png", "Image_2.png"));
    EXPECT_FALSE(GlobMatch::matches("Image_?.png", "Image_2121.png"));
    EXPECT_FALSE(GlobMatch::matches("Image_?.png", "Image_.png"));
}

TEST(GlobMatch, MatchingIsAsciiCaseInsensitive)
{
    EXPECT_TRUE(GlobMatch::matches("*.EXE", "setup.exe"));
    EXPECT_TRUE(GlobMatch::matches("*.EXE", "SETUP.EXE"));
}

TEST(GlobMatch, PrefixStarMatchesSuffix)
{
    EXPECT_TRUE(GlobMatch::matches("*report.txt", "quarterly_report.txt"));
    EXPECT_FALSE(GlobMatch::matches("*report.txt", "report.doc"));
}

TEST(GlobMatch, SuffixStarMatchesPrefix)
{
    EXPECT_TRUE(GlobMatch::matches("doc*", "document.txt"));
    EXPECT_FALSE(GlobMatch::matches("doc*", "other.txt"));
}

TEST(GlobMatch, QuestionMarkCountsUnicodeCodepointsNotUtf8Bytes)
{
    // "é" is one codepoint but two UTF-8 bytes -- a single "?" must consume it as one character,
    // not match only half of it.
    EXPECT_TRUE(GlobMatch::matches("?", "\xC3\xA9"));            // "é" -- one codepoint
    EXPECT_FALSE(GlobMatch::matches("?", "\xC3\xA9\xC3\xA9"));   // "éé" -- two codepoints, too many

    EXPECT_TRUE(GlobMatch::matches("??", "\xC3\xA9\xC3\xA9"));   // "éé" -- exactly two codepoints
}

TEST(GlobMatch, NoWildcardsRequiresExactFullMatch)
{
    EXPECT_TRUE(GlobMatch::matches("report.txt", "report.txt"));
    EXPECT_FALSE(GlobMatch::matches("report.txt", "quarterly_report.txt"));
}
