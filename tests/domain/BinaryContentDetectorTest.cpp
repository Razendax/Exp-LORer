#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "BinaryContentDetector.h"

namespace
{
    std::vector<std::byte> toBytes(const std::string& text)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (char c : text)
        {
            bytes.push_back(static_cast<std::byte>(c));
        }
        return bytes;
    }
}

TEST(BinaryContentDetector, PlainTextHasNoNulByte)
{
    EXPECT_FALSE(BinaryContentDetector::containsNulByte(toBytes("hello world")));
}

TEST(BinaryContentDetector, EmptyIsNotBinary)
{
    EXPECT_FALSE(BinaryContentDetector::containsNulByte({}));
}

TEST(BinaryContentDetector, DetectsEmbeddedNulByte)
{
    std::vector<std::byte> bytes = toBytes("hello");
    bytes.push_back(std::byte{ 0 });
    bytes.push_back(std::byte{ 'x' });

    EXPECT_TRUE(BinaryContentDetector::containsNulByte(bytes));
}
