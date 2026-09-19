#include <gtest/gtest.h>

#include "PathUtf8.h"

TEST(PathUtf8, ConvertsAccentedLatinCharacterWithoutThrowing)
{
    const std::filesystem::path path = std::filesystem::path(std::u8string(u8"café.txt"));

    std::string utf8;
    EXPECT_NO_THROW(utf8 = PathUtf8::toUtf8(path));
    EXPECT_EQ(utf8, std::string(reinterpret_cast<const char*>(u8"café.txt")));
}

TEST(PathUtf8, ConvertsCjkCharacterWithoutThrowing)
{
    const std::filesystem::path path = std::filesystem::path(std::u8string(u8"文件.txt"));

    std::string utf8;
    EXPECT_NO_THROW(utf8 = PathUtf8::toUtf8(path));
    EXPECT_EQ(utf8, std::string(reinterpret_cast<const char*>(u8"文件.txt")));
}

TEST(PathUtf8, ConvertsAstralPlaneEmojiWithoutThrowing)
{
    const std::filesystem::path path = std::filesystem::path(std::u8string(u8"📁folder.txt"));

    std::string utf8;
    EXPECT_NO_THROW(utf8 = PathUtf8::toUtf8(path));
    EXPECT_EQ(utf8, std::string(reinterpret_cast<const char*>(u8"📁folder.txt")));
}
