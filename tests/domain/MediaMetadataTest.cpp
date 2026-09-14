#include <gtest/gtest.h>

#include "MediaMetadata.h"

TEST(MediaMetadata, CreateSucceedsWithValidFields)
{
    auto result = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(60000), "h264");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().width(), 1920);
    EXPECT_EQ(result.value().height(), 1080);
    EXPECT_EQ(result.value().duration(), std::chrono::milliseconds(60000));
    EXPECT_EQ(result.value().codec(), "h264");
}

TEST(MediaMetadata, CreateSucceedsWithZeroDurationForStillImage)
{
    auto result = MediaMetadata::create(800, 600, std::chrono::milliseconds(0), "jpeg");

    EXPECT_TRUE(result.hasValue());
}

TEST(MediaMetadata, CreateFailsWithNonPositiveWidth)
{
    auto result = MediaMetadata::create(0, 600, std::chrono::milliseconds(0), "jpeg");

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(MediaMetadata, CreateFailsWithNonPositiveHeight)
{
    auto result = MediaMetadata::create(800, -1, std::chrono::milliseconds(0), "jpeg");

    EXPECT_TRUE(result.hasError());
}

TEST(MediaMetadata, CreateFailsWithNegativeDuration)
{
    auto result = MediaMetadata::create(800, 600, std::chrono::milliseconds(-1), "jpeg");

    EXPECT_TRUE(result.hasError());
}
