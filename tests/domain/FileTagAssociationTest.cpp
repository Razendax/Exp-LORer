#include <gtest/gtest.h>

#include "FileTagAssociation.h"

TEST(FileTagAssociation, CreateSucceedsWithValidFields)
{
    auto result = FileTagAssociation::create("C:/data/report.pdf", std::optional<std::uint64_t>(0x1234), 1);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().filePath(), "C:/data/report.pdf");
    ASSERT_TRUE(result.value().fileHash().has_value());
    EXPECT_EQ(result.value().fileHash().value(), 0x1234u);
    EXPECT_EQ(result.value().tagId(), 1);
}

TEST(FileTagAssociation, CreateSucceedsWithoutHash)
{
    auto result = FileTagAssociation::create("C:/data/report.pdf", std::nullopt, 1);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().fileHash().has_value());
}

TEST(FileTagAssociation, CreateFailsWithEmptyPath)
{
    auto result = FileTagAssociation::create("", std::nullopt, 1);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(FileTagAssociation, CreateFailsWithUnassignedTagId)
{
    auto result = FileTagAssociation::create("C:/data/report.pdf", std::nullopt, Tag::kUnassignedId);

    EXPECT_TRUE(result.hasError());
}

TEST(FileTagAssociation, EqualityComparesAllFields)
{
    auto a = FileTagAssociation::create("C:/data/report.pdf", std::nullopt, 1).value();
    auto b = FileTagAssociation::create("C:/data/report.pdf", std::nullopt, 1).value();
    auto c = FileTagAssociation::create("C:/data/report.pdf", std::nullopt, 2).value();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
