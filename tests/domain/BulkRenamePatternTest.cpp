#include <gtest/gtest.h>

#include "BulkRenamePattern.h"

TEST(BulkRenamePattern, PassthroughWhenNoStagesEnabled)
{
    BulkRenamePattern pattern;
    auto result = pattern.apply("vacation", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "vacation.jpg");
}

TEST(BulkRenamePattern, PlainFindReplace)
{
    BulkRenamePattern pattern;
    pattern.findText = "vacation";
    pattern.replaceText = "trip";

    auto result = pattern.apply("vacation photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "trip photo.jpg");
}

TEST(BulkRenamePattern, PlainFindReplaceIsCaseInsensitiveByDefault)
{
    BulkRenamePattern pattern;
    pattern.findText = "VACATION";
    pattern.replaceText = "trip";

    auto result = pattern.apply("Vacation Photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "trip Photo.jpg");
}

TEST(BulkRenamePattern, PlainFindReplaceRespectsMatchCase)
{
    BulkRenamePattern pattern;
    pattern.findText = "VACATION";
    pattern.replaceText = "trip";
    pattern.matchCase = true;

    auto result = pattern.apply("Vacation Photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "Vacation Photo.jpg");
}

TEST(BulkRenamePattern, RegexReplaceWithBackreference)
{
    BulkRenamePattern pattern;
    pattern.useRegex = true;
    pattern.findText = "IMG_(\\d+)";
    pattern.replaceText = "Photo-$1";

    auto result = pattern.apply("IMG_042", ".png", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "Photo-042.png");
}

TEST(BulkRenamePattern, InvalidRegexReturnsFailure)
{
    BulkRenamePattern pattern;
    pattern.useRegex = true;
    pattern.findText = "[unterminated";

    auto result = pattern.apply("anything", ".txt", 0);
    EXPECT_TRUE(result.hasError());
}

TEST(BulkRenamePattern, CaseTransformUpper)
{
    BulkRenamePattern pattern;
    pattern.caseTransform = BulkRenamePattern::CaseTransform::Upper;

    auto result = pattern.apply("café report", ".txt", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "CAFÉ REPORT.txt");
}

TEST(BulkRenamePattern, CaseTransformLower)
{
    BulkRenamePattern pattern;
    pattern.caseTransform = BulkRenamePattern::CaseTransform::Lower;

    auto result = pattern.apply("CAFÉ REPORT", ".txt", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "café report.txt");
}

TEST(BulkRenamePattern, CaseTransformTitle)
{
    BulkRenamePattern pattern;
    pattern.caseTransform = BulkRenamePattern::CaseTransform::Title;

    auto result = pattern.apply("vacation photo set", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "Vacation Photo Set.jpg");
}

TEST(BulkRenamePattern, CounterSuffixWithPaddingAndStep)
{
    BulkRenamePattern pattern;
    pattern.counterEnabled = true;
    pattern.counterStart = 10;
    pattern.counterStep = 5;
    pattern.counterDigits = 3;
    pattern.counterPosition = BulkRenamePattern::CounterPosition::Suffix;
    pattern.counterSeparator = "_";

    auto first = pattern.apply("photo", ".jpg", 0);
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(first.value(), "photo_010.jpg");

    auto second = pattern.apply("photo", ".jpg", 1);
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value(), "photo_015.jpg");
}

TEST(BulkRenamePattern, CounterPrefix)
{
    BulkRenamePattern pattern;
    pattern.counterEnabled = true;
    pattern.counterStart = 1;
    pattern.counterDigits = 2;
    pattern.counterPosition = BulkRenamePattern::CounterPosition::Prefix;
    pattern.counterSeparator = "-";

    auto result = pattern.apply("photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "01-photo.jpg");
}

TEST(BulkRenamePattern, ExtensionOverrideReplacesExtension)
{
    BulkRenamePattern pattern;
    pattern.newExtension = "png";

    auto result = pattern.apply("photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "photo.png");
}

TEST(BulkRenamePattern, ExtensionOverrideAcceptsLeadingDot)
{
    BulkRenamePattern pattern;
    pattern.newExtension = ".png";

    auto result = pattern.apply("photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "photo.png");
}

TEST(BulkRenamePattern, ExtensionOverrideToEmptyRemovesExtension)
{
    BulkRenamePattern pattern;
    pattern.newExtension = "";

    auto result = pattern.apply("photo", ".jpg", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "photo");
}

TEST(BulkRenamePattern, StagesApplyInFixedOrder)
{
    // find/replace -> case transform -> counter -> extension, all at once.
    BulkRenamePattern pattern;
    pattern.findText = "draft";
    pattern.replaceText = "final";
    pattern.caseTransform = BulkRenamePattern::CaseTransform::Upper;
    pattern.counterEnabled = true;
    pattern.counterStart = 1;
    pattern.counterSeparator = "_";
    pattern.newExtension = "pdf";

    auto result = pattern.apply("draft", ".docx", 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), "FINAL_1.pdf");
}
