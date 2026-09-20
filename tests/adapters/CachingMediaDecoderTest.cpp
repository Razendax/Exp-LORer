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
        MOCK_METHOD(Result<MediaMetadata>, extractMetadata, (const std::filesystem::path& mediaFile), (const, override));
        MOCK_METHOD(Result<std::vector<std::byte>>, generateThumbnail,
                    (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight), (const, override));
    };

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

TEST_F(CachingMediaDecoderTest, ExtractMetadataPassesThroughUncached)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    MediaMetadata metadata = MediaMetadata::create(100, 100, std::chrono::milliseconds(0), "image").value();

    EXPECT_CALL(inner, extractMetadata(m_filePath)).Times(2).WillRepeatedly(Return(Result<MediaMetadata>::success(metadata)));

    CachingMediaDecoder decorator(inner, cache);
    decorator.extractMetadata(m_filePath);
    decorator.extractMetadata(m_filePath);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailCallsInnerOnceForRepeatedCalls)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    std::vector<std::byte> bytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128))
        .Times(1)
        .WillOnce(Return(Result<std::vector<std::byte>>::success(bytes)));

    CachingMediaDecoder decorator(inner, cache);

    auto first = decorator.generateThumbnail(m_filePath, 128, 128);
    auto second = decorator.generateThumbnail(m_filePath, 128, 128);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value(), bytes);
    EXPECT_EQ(second.value(), bytes);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailWithDifferentDimensionsCallsInnerAgain)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    std::vector<std::byte> smallBytes{ std::byte{ 0x01 } };
    std::vector<std::byte> largeBytes{ std::byte{ 0x02 } };

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 64, 64)).WillOnce(Return(Result<std::vector<std::byte>>::success(smallBytes)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 256, 256)).WillOnce(Return(Result<std::vector<std::byte>>::success(largeBytes)));

    CachingMediaDecoder decorator(inner, cache);

    auto small = decorator.generateThumbnail(m_filePath, 64, 64);
    auto large = decorator.generateThumbnail(m_filePath, 256, 256);

    ASSERT_TRUE(small.hasValue());
    ASSERT_TRUE(large.hasValue());
    EXPECT_EQ(small.value(), smallBytes);
    EXPECT_EQ(large.value(), largeBytes);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailFailureIsNotCached)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128))
        .Times(2)
        .WillRepeatedly(Return(Result<std::vector<std::byte>>::failure(Error(ErrorCode::IoError, "decode failed"))));

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
        .WillOnce(Return(Result<std::vector<std::byte>>::failure(Error(ErrorCode::NotFound, "missing"))));

    CachingMediaDecoder decorator(inner, cache);
    auto result = decorator.generateThumbnail(missing, 128, 128);

    EXPECT_TRUE(result.hasError());
}
