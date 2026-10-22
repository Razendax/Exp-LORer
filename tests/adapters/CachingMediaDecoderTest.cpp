#include <gtest/gtest.h>

#include <cstring>
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
                    (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight, int previewIndex), (const, override));
        MOCK_METHOD(std::vector<Result<MediaThumbnail>>, generateThumbnails,
                    (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight, const std::vector<int>& previewIndices),
                    (const, override));
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

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0))
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

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0))
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

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 64, 64, 0)).WillOnce(Return(Result<MediaThumbnail>::success(smallThumbnail)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 256, 256, 0)).WillOnce(Return(Result<MediaThumbnail>::success(largeThumbnail)));

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

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0))
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

    EXPECT_CALL(inner, generateThumbnail(missing, 128, 128, 0))
        .WillOnce(Return(Result<MediaThumbnail>::failure(Error(ErrorCode::NotFound, "missing"))));

    CachingMediaDecoder decorator(inner, cache);
    auto result = decorator.generateThumbnail(missing, 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailWithDifferentPreviewIndexCallsInnerAgainAndBothAreCacheable)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail frame0 = makeThumbnail({ std::byte{ 0x01 } });
    const MediaThumbnail frame4 = makeThumbnail({ std::byte{ 0x02 } });

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0))
        .Times(1)
        .WillOnce(Return(Result<MediaThumbnail>::success(frame0)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 4))
        .Times(1)
        .WillOnce(Return(Result<MediaThumbnail>::success(frame4)));

    CachingMediaDecoder decorator(inner, cache);

    // Each previewIndex decoded once...
    auto first0 = decorator.generateThumbnail(m_filePath, 128, 128, 0);
    auto first4 = decorator.generateThumbnail(m_filePath, 128, 128, 4);
    // ...and a repeated request for either hits its own cache entry without calling inner again
    // (the inner EXPECT_CALLs above are each Times(1)) -- a hit for one previewIndex must not
    // short-circuit a request for the other.
    auto second0 = decorator.generateThumbnail(m_filePath, 128, 128, 0);
    auto second4 = decorator.generateThumbnail(m_filePath, 128, 128, 4);

    ASSERT_TRUE(first0.hasValue());
    ASSERT_TRUE(first4.hasValue());
    ASSERT_TRUE(second0.hasValue());
    ASSERT_TRUE(second4.hasValue());
    EXPECT_EQ(first0.value().bytes, frame0.bytes);
    EXPECT_EQ(first4.value().bytes, frame4.bytes);
    EXPECT_EQ(second0.value().bytes, frame0.bytes);
    EXPECT_EQ(second4.value().bytes, frame4.bytes);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailsWithMixedHitAndMissCallsInnerBatchOnceForOnlyTheMisses)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail frame0 = makeThumbnail({ std::byte{ 0x01 } });
    const MediaThumbnail frame2 = makeThumbnail({ std::byte{ 0x02 } });
    const MediaThumbnail frame4 = makeThumbnail({ std::byte{ 0x03 } });

    // Warm the cache for indices 0 and 2 only, via the single-frame path.
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0)).WillOnce(Return(Result<MediaThumbnail>::success(frame0)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 2)).WillOnce(Return(Result<MediaThumbnail>::success(frame2)));

    CachingMediaDecoder decorator(inner, cache);
    decorator.generateThumbnail(m_filePath, 128, 128, 0);
    decorator.generateThumbnail(m_filePath, 128, 128, 2);

    // A batch for {0, 2, 4} must hit the cache for 0 and 2, and call the inner batched method
    // exactly once with only the missing index 4.
    EXPECT_CALL(inner, generateThumbnails(m_filePath, 128, 128, std::vector<int>{ 4 }))
        .Times(1)
        .WillOnce(Return(std::vector<Result<MediaThumbnail>>{ Result<MediaThumbnail>::success(frame4) }));

    auto results = decorator.generateThumbnails(m_filePath, 128, 128, { 0, 2, 4 });

    ASSERT_EQ(results.size(), 3u);
    ASSERT_TRUE(results[0].hasValue());
    ASSERT_TRUE(results[1].hasValue());
    ASSERT_TRUE(results[2].hasValue());
    EXPECT_EQ(results[0].value().bytes, frame0.bytes);
    EXPECT_EQ(results[1].value().bytes, frame2.bytes);
    EXPECT_EQ(results[2].value().bytes, frame4.bytes);
}

// A blob written by a pre-pageCount binary (old MetadataHeader shape: no formatVersion/pageCount
// fields) must be treated as a cache miss and regenerated, not misread at the new, larger fixed
// offsets (Architecture.md §14.101).
TEST_F(CachingMediaDecoderTest, GenerateThumbnailWithOldFormatBlobTreatsItAsCacheMissAndRegenerates)
{
    struct OldMetadataHeader
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int64_t durationMs = 0;
        std::int64_t bitRateBps = 0;
        std::uint32_t codecLength = 0;
    };

    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail thumbnail = makeThumbnail({ std::byte{ 0xAA }, std::byte{ 0xBB } }, 640, 480);

    std::error_code ec;
    const std::uintmax_t size = fs::file_size(m_filePath, ec);
    const auto mtime = fs::last_write_time(m_filePath, ec);
    const ThumbnailCache::Key key{ m_filePath, size, mtime.time_since_epoch().count(), 128, 128, 0 };

    const std::string codec = "image";
    OldMetadataHeader oldHeader;
    oldHeader.width = 1920;
    oldHeader.height = 1080;
    oldHeader.codecLength = static_cast<std::uint32_t>(codec.size());

    std::vector<std::byte> oldBlob(sizeof(OldMetadataHeader) + codec.size());
    std::memcpy(oldBlob.data(), &oldHeader, sizeof(OldMetadataHeader));
    std::memcpy(oldBlob.data() + sizeof(OldMetadataHeader), codec.data(), codec.size());
    cache.put(key, oldBlob);

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0))
        .Times(1)
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    CachingMediaDecoder decorator(inner, cache);
    auto result = decorator.generateThumbnail(m_filePath, 128, 128);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().bytes, thumbnail.bytes);
    EXPECT_EQ(result.value().metadata.width(), 640);
}

TEST_F(CachingMediaDecoderTest, GenerateThumbnailsWithAllCacheHitsNeverCallsInnerBatch)
{
    MockInnerDecoder inner;
    ThumbnailCache cache(m_tempDir / "cache");
    const MediaThumbnail frame0 = makeThumbnail({ std::byte{ 0x01 } });
    const MediaThumbnail frame1 = makeThumbnail({ std::byte{ 0x02 } });

    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 0)).WillOnce(Return(Result<MediaThumbnail>::success(frame0)));
    EXPECT_CALL(inner, generateThumbnail(m_filePath, 128, 128, 1)).WillOnce(Return(Result<MediaThumbnail>::success(frame1)));

    CachingMediaDecoder decorator(inner, cache);
    decorator.generateThumbnail(m_filePath, 128, 128, 0);
    decorator.generateThumbnail(m_filePath, 128, 128, 1);

    EXPECT_CALL(inner, generateThumbnails(_, _, _, _)).Times(0);

    auto results = decorator.generateThumbnails(m_filePath, 128, 128, { 0, 1 });

    ASSERT_EQ(results.size(), 2u);
    ASSERT_TRUE(results[0].hasValue());
    ASSERT_TRUE(results[1].hasValue());
    EXPECT_EQ(results[0].value().bytes, frame0.bytes);
    EXPECT_EQ(results[1].value().bytes, frame1.bytes);
}
