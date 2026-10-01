#include <gtest/gtest.h>

#include "RegexMatch.h"

TEST(RegexMatch, MatchesSimplePattern)
{
    auto result = RegexMatch::matches("^Img\\d+", "Img123.png");
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value());

    result = RegexMatch::matches("^Img\\d+", "Vacation.png");
    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value());
}

TEST(RegexMatch, CaseInsensitiveByDefault)
{
    auto result = RegexMatch::matches("^file", "File Name.txt");
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value());
}

TEST(RegexMatch, CaseSensitiveWhenRequested)
{
    auto result = RegexMatch::matches("^file", "File Name.txt", true);
    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value());

    result = RegexMatch::matches("^File", "File Name.txt", true);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value());
}

TEST(RegexMatch, InvalidPatternReturnsFailureNotException)
{
    Result<bool> result = Result<bool>::success(false);
    EXPECT_NO_THROW(result = RegexMatch::matches("[unterminated", "anything"));
    EXPECT_TRUE(result.hasError());
}

TEST(RegexMatch, MatchesNonAsciiNameWithoutThrowing)
{
    const std::u8string nameUtf8 = u8"Café report.txt";
    const std::string name(nameUtf8.begin(), nameUtf8.end());

    Result<bool> result = Result<bool>::success(false);
    EXPECT_NO_THROW(result = RegexMatch::matches("report", name));
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value());
}
