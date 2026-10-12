#include <gtest/gtest.h>

#include "Tag.h"

TEST(Tag, CreateSucceedsWithValidNameAndColor)
{
    auto result = Tag::create(1, "Work", "#FF8800");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().id(), 1);
    EXPECT_EQ(result.value().name(), "Work");
    EXPECT_EQ(result.value().hexColor(), "#FF8800");
}

TEST(Tag, CreateFailsWithEmptyName)
{
    auto result = Tag::create(1, "", "#FF8800");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(Tag, CreateFailsWithLeadingSlash)
{
    auto result = Tag::create(1, "/game", "#FF8800");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(Tag, CreateFailsWithTrailingSlash)
{
    auto result = Tag::create(1, "game/", "#FF8800");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(Tag, CreateFailsWithEmptySegment)
{
    auto result = Tag::create(1, "game//action", "#FF8800");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(Tag, CreateSucceedsWithNestedName)
{
    auto result = Tag::create(1, "game/action", "#FF8800");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().name(), "game/action");
}

TEST(Tag, CreateFailsWithMissingHash)
{
    auto result = Tag::create(1, "Work", "FF8800");

    EXPECT_TRUE(result.hasError());
}

TEST(Tag, CreateFailsWithWrongLength)
{
    auto result = Tag::create(1, "Work", "#FF88");

    EXPECT_TRUE(result.hasError());
}

TEST(Tag, CreateFailsWithNonHexDigits)
{
    auto result = Tag::create(1, "Work", "#GGHHII");

    EXPECT_TRUE(result.hasError());
}

TEST(Tag, EqualityComparesAllFields)
{
    auto a = Tag::create(1, "Work", "#FF8800").value();
    auto b = Tag::create(1, "Work", "#FF8800").value();
    auto c = Tag::create(2, "Work", "#FF8800").value();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
