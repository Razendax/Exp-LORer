#include <gtest/gtest.h>

#include <fstream>

#include <gmock/gmock.h>

#include "CachingMediaDecoder.h"
#include "IMediaDecoder.h"

using ::testing::Return;
using ::testing::_;

namespace
{
    namespace fs = std::filesystem;

    class MockInnerDecoder : public IMediaDecoder
    {
    public:
        MOCK_METHOD(Result<MediaThumbnail>, generateThumbnail,
                    (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight), (const, override));
    };

    MediaThumbnail makeThumbnail(std::vector<std::byte> bytes, int width = 100, int height = 100)
    {
        MediaThumbnail thumbnail;
        thumbnail.bytes = std::move(bytes);
        thumbnail.metadata = MediaMetadata::create(width, height, std::chrono::milliseconds(0), "image").value();
        return thumbnail;
    }

    class CachingMediaDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_caching_media_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);

            m_filePath = m_tempDir / "photo.jpg";
            std::ofstream stream(m_filePath, std::ios::binary);
            stream << "fake image bytes";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_filePath;
    };
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailCallsInnerOnceForRepeatedCalls)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail thumbnail = makeThumbnail({ std::byte{ 0xFF }, std::byte{ 0xD8 } }, 1920, 1080);

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128))
        .Times(1)
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    CachingMediaDecoder decorator(inner, cache);

    auto first = decorator.generateThumbnail(m_filePath, 128, 128);
    auto second = decorator.generateThumbnail(m_filePath, 128, 128);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value().bytes, thumbnail.bytes);
    EXPECT_EQ(second.value().bytes, thumbnail.bytes);
}

// A cache hit must return the same metadata the original decode produced -- without this, every
// cache hit would still need to fall back into the inner decoder to get width/height/duration,
// defeating most of the point of caching (Architecture.md §14.63).
TEST_F(CachingMediaDecoderTest, GenerateThumbnailCacheHitReturnsCachedMetadataWithoutCallingInnerAgain)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail thumbnail = makeThumbnail({ std::byte{ 0xFF }, std::byte{ 0xD8 } }, 1920, 1080);

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128))
        .Times(1)
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    CachingMediaDecoder decorator(inner, cache);

    decorator.generateThumbnail(m_filePath, 128, 128);
    auto cached = decorator.generateThumbnail(m_filePath, 128, 128);

    ASSERT_TRUE(cached.hasValue());
    EXPECT_EQ(cached.value().metadata.width(), 1920);
    EXPECT_EQ(cached.value().metadata.height(), 1080);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailWithDifferentDimensionsCallsInnerAgain)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail smallThumbnail = makeThumbnail({ std::byte{ 0x01 } });
    const MediaThumbnail largeThumbnail = makeThumbnail({ std::byte{ 0x02 } });

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 64, 64)).WillOnce(Return(Result<MediaThumbnail>::success(smallThumbnail)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 256, 256)).WillOnce(Return(Result<MediaThumbnail>::success(largeThumbnail)));

    CachingMediaDecoder decorator(inner, cache);

    auto small = decorator.generateThumbnail(m_filePath, 64, 64);
    auto large = decorator.generateThumbnail(m_filePath, 256, 256);

    ASSERT_TRUE(small.hasValue());
    ASSERT_TRUE(large.hasValue());
    EXPECT_EQ(small.value().bytes, smallThumbnail.bytes);
    EXPECT_EQ(large.value().bytes, largeThumbnail.bytes);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailFailureIsNotCached)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128))
        .Times(2)
        .WillRepeatedly(Return(Result<MediaThumbnail>::failure(Error(ErrorCode::IoError, "decode failed"))));

    CachingMediaDecoder decorator(inner, cache);

    EXPECT_TRUE(decorator.generateThumbnail(m_filePath, 128, 128).hasError());
    EXPECT_TRUE(decorator.generateThumbnail(m_filePath, 128, 128).hasError());
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailForMissingFileFallsThroughToInner)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const fs::path missing = m_tempDir / "does-not-exist.jpg";

    EXPECT_CALL(inner, generateThumbnail(missing, 128, 128))
        .WillOnce(Return(Result<MediaThumbnail>::failure(Error(ErrorCode::NotFound, "missing"))));

    CachingMediaDecoder decorator(inner, cache);
    auto result = decorator.generateThumbnail(missing, 128, 128);

    EXPECT_TRUE(result.hasError());
}
