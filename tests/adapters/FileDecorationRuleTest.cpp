#include <gtest/gtest.h>

#include "FileDecorationRule.h"

namespace
{
    FileDecorationRule makeRule(std::string patternsRaw)
    {
        Result<FileDecorationRule> result = FileDecorationRule::create(std::move(patternsRaw), std::nullopt, std::nullopt, std::nullopt,
                                                                         false, false, false, false);
        EXPECT_TRUE(result);
        return std::move(result).value();
    }
}

TEST(FileDecorationRule, MatchesAnyPatternInACommaSeparatedList)
{
    const FileDecorationRule rule = makeRule("*.jpg,*.png,*.gif");

    EXPECT_TRUE(rule.matches("photo.jpg", false));
    EXPECT_TRUE(rule.matches("photo.png", false));
    EXPECT_TRUE(rule.matches("photo.gif", false));
    EXPECT_FALSE(rule.matches("photo.bmp", false));
}

TEST(FileDecorationRule, QuotedPatternKeepsALiteralCommaTogether)
{
    const FileDecorationRule rule = makeRule("\"Image, file.png\"");

    EXPECT_TRUE(rule.matches("Image, file.png", false));
    EXPECT_FALSE(rule.matches("Image", false));
    EXPECT_FALSE(rule.matches("file.png", false));
}

TEST(FileDecorationRule, DoubledQuoteInsideAQuotedSpanIsALiteralQuote)
{
    const FileDecorationRule rule = makeRule("\"say \"\"hi\"\".txt\"");

    EXPECT_TRUE(rule.matches("say \"hi\".txt", false));
}

TEST(FileDecorationRule, UnterminatedQuoteFailsToParse)
{
    const Result<FileDecorationRule> result = FileDecorationRule::create("\"unterminated", std::nullopt, std::nullopt, std::nullopt,
                                                                           false, false, false, false);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(FileDecorationRule, EmptyTokensFromStrayCommasAreSkipped)
{
    const FileDecorationRule rule = makeRule("*.exe,,*.dll,");

    EXPECT_TRUE(rule.matches("setup.exe", false));
    EXPECT_TRUE(rule.matches("lib.dll", false));
    // An empty pattern token never matches anything -- confirms the stray commas produced no
    // "matches everything" pattern.
    EXPECT_FALSE(rule.matches("", false));
}

TEST(FileDecorationRule, StyleFieldsRoundTripThroughCreate)
{
    const Result<FileDecorationRule> result =
        FileDecorationRule::create("*.md", std::string("#FF0000"), std::string("Consolas"), 14, true, true, true, true);

    ASSERT_TRUE(result);
    const FileDecorationRule& rule = result.value();
    EXPECT_EQ(rule.hexColor(), "#FF0000");
    EXPECT_EQ(rule.fontFamily(), "Consolas");
    EXPECT_EQ(rule.fontPointSize(), 14);
    EXPECT_TRUE(rule.bold());
    EXPECT_TRUE(rule.italic());
    EXPECT_TRUE(rule.underline());
    EXPECT_TRUE(rule.strikeout());
}

TEST(FileDecorationRule, FolderOnlyPatternDoesNotMatchAFile)
{
    const FileDecorationRule rule = makeRule("#.git");

    EXPECT_TRUE(rule.matches(".git", true));
    EXPECT_FALSE(rule.matches(".git", false));
}
