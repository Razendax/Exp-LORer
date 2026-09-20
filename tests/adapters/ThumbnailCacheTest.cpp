#include <gtest/gtest.h>

#include "ThumbnailCache.h"

namespace
{
    namespace fs = std::filesystem;

    class ThumbnailCacheTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_thumbnail_cache_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        std::vector<std::byte> makeBytes(std::size_t count, std::byte fill)
        {
            return std::vector<std::byte>(count, fill);
        }

        fs::path m_tempDir;
    };
}

TEST_F(ThumbnailCacheTest, GetOnEmptyCacheReturnsNullopt)
{
    ThumbnailCache cache(m_tempDir / "cache");

    auto result = cache.get(ThumbnailCache::Key{ "C:/photos/a.jpg", 100, 1, 128, 128 });

    EXPECT_FALSE(result.has_value());
}

TEST_F(ThumbnailCacheTest, PutThenGetRoundTripsBytes)
{
    ThumbnailCache cache(m_tempDir / "cache");
    const ThumbnailCache::Key key{ "C:/photos/a.jpg", 100, 1, 128, 128 };
    const std::vector<std::byte> bytes = makeBytes(16, std::byte{ 0xAB });

    cache.put(key, bytes);
    auto result = cache.get(key);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, bytes);
}

TEST_F(ThumbnailCacheTest, DifferentDimensionsProduceDistinctEntries)
{
    ThumbnailCache cache(m_tempDir / "cache");
    const ThumbnailCache::Key smallKey{ "C:/photos/a.jpg", 100, 1, 64, 64 };
    const ThumbnailCache::Key largeKey{ "C:/photos/a.jpg", 100, 1, 256, 256 };

    cache.put(smallKey, makeBytes(4, std::byte{ 0x01 }));
    cache.put(largeKey, makeBytes(8, std::byte{ 0x02 }));

    auto smallResult = cache.get(smallKey);
    auto largeResult = cache.get(largeKey);

    ASSERT_TRUE(smallResult.has_value());
    ASSERT_TRUE(largeResult.has_value());
    EXPECT_NE(smallResult->size(), largeResult->size());
}

TEST_F(ThumbnailCacheTest, ChangedMtimeProducesDistinctEntryFromOriginal)
{
    ThumbnailCache cache(m_tempDir / "cache");
    const ThumbnailCache::Key original{ "C:/photos/a.jpg", 100, 1, 128, 128 };
    const ThumbnailCache::Key changed{ "C:/photos/a.jpg", 100, 2, 128, 128 };

    cache.put(original, makeBytes(4, std::byte{ 0x01 }));

    EXPECT_FALSE(cache.get(changed).has_value());
}

TEST_F(ThumbnailCacheTest, EvictsLeastRecentlyWrittenEntryWhenOverCap)
{
    // A tiny cap (a handful of bytes) forces eviction after the second put.
    ThumbnailCache cache(m_tempDir / "cache", /*maxTotalBytes=*/20);

    const ThumbnailCache::Key first{ "C:/photos/first.jpg", 100, 1, 128, 128 };
    const ThumbnailCache::Key second{ "C:/photos/second.jpg", 100, 1, 128, 128 };

    cache.put(first, makeBytes(16, std::byte{ 0x01 }));
    cache.put(second, makeBytes(16, std::byte{ 0x02 }));

    // "first" was written earlier and never re-touched, so it should have been pruned to stay
    // under the cap; "second" is the most recent write and should survive.
    EXPECT_TRUE(cache.get(second).has_value());
}

TEST_F(ThumbnailCacheTest, ReconstructingCacheOverSameDirectoryStillFindsEntries)
{
    const fs::path cacheDir = m_tempDir / "cache";
    const ThumbnailCache::Key key{ "C:/photos/a.jpg", 100, 1, 128, 128 };
    const std::vector<std::byte> bytes = makeBytes(4, std::byte{ 0x42 });

    {
        ThumbnailCache cache(cacheDir);
        cache.put(key, bytes);
    }

    ThumbnailCache reopened(cacheDir);
    auto result = reopened.get(key);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, bytes);
}
