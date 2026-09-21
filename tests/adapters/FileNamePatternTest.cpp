#include <gtest/gtest.h>

#include "FileNamePattern.h"

TEST(FileNamePattern, StarMatchesAnyRunIncludingNone)
{
    const FileNamePattern pattern("*.exe");

    EXPECT_TRUE(pattern.matches("setup.exe", false));
    EXPECT_TRUE(pattern.matches(".exe", false));
    EXPECT_FALSE(pattern.matches("setup.dll", false));
}

TEST(FileNamePattern, StarPatternNeverMatchesADirectoryByDefault)
{
    const FileNamePattern pattern("*.exe");

    EXPECT_FALSE(pattern.matches("setup.exe", true));
}

TEST(FileNamePattern, QuestionMarkMatchesExactlyOneCharacter)
{
    const FileNamePattern pattern("Image_?.png");

    EXPECT_TRUE(pattern.matches("Image_1.png", false));
    EXPECT_TRUE(pattern.matches("Image_2.png", false));
    EXPECT_FALSE(pattern.matches("Image_2121.png", false));
    EXPECT_FALSE(pattern.matches("Image_.png", false));
}

TEST(FileNamePattern, MatchingIsAsciiCaseInsensitive)
{
    const FileNamePattern pattern("*.EXE");

    EXPECT_TRUE(pattern.matches("setup.exe", false));
    EXPECT_TRUE(pattern.matches("SETUP.EXE", false));
}

TEST(FileNamePattern, HashPrefixMarksPatternAsFolderOnlyAndIsStripped)
{
    const FileNamePattern pattern("#.git");

    EXPECT_TRUE(pattern.appliesToFolders());
    EXPECT_TRUE(pattern.matches(".git", true));
    EXPECT_FALSE(pattern.matches(".git", false));
}

TEST(FileNamePattern, PatternWithoutHashAppliesToFilesOnly)
{
    const FileNamePattern pattern("*.txt");

    EXPECT_FALSE(pattern.appliesToFolders());
    EXPECT_TRUE(pattern.matches("notes.txt", false));
    EXPECT_FALSE(pattern.matches("notes.txt", true));
}

TEST(FileNamePattern, QuestionMarkCountsUnicodeCodepointsNotUtf8Bytes)
{
    // "é" is one codepoint but two UTF-8 bytes -- a single "?" must consume it as one character,
    // not match only half of it.
    const FileNamePattern singleChar("?");
    EXPECT_TRUE(singleChar.matches("\xC3\xA9", false));            // "é" -- one codepoint
    EXPECT_FALSE(singleChar.matches("\xC3\xA9\xC3\xA9", false));   // "éé" -- two codepoints, too many

    const FileNamePattern twoChars("??");
    EXPECT_TRUE(twoChars.matches("\xC3\xA9\xC3\xA9", false));      // "éé" -- exactly two codepoints
}
