#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

#include "ArchiveFixture.h"
#include "ArchiveIndexCache.h"

namespace
{
    namespace fs = std::filesystem;

    class ArchiveIndexCacheTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_archive_index_cache_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        ArchiveIndexCache m_cache;
    };
}

TEST_F(ArchiveIndexCacheTest, ReturnsEntriesOnFirstCall)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" } });

    auto result = m_cache.entriesFor(archivePath);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 1u);
}

TEST_F(ArchiveIndexCacheTest, ReturnsSameEntriesOnRepeatedCallsWithoutFileChange)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" }, { "notes.txt", "world" } });

    auto first = m_cache.entriesFor(archivePath);
    auto second = m_cache.entriesFor(archivePath);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value().size(), second.value().size());
}

TEST_F(ArchiveIndexCacheTest, RescansAfterArchiveFileChanges)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" } });

    auto first = m_cache.entriesFor(archivePath);
    ASSERT_TRUE(first.hasValue());
    ASSERT_EQ(first.value().size(), 1u);

    // Sleep briefly so the rewritten archive's mtime/size differ from the cached key -- a changed
    // source file naturally produces a different cache key (same posture as ThumbnailCache).
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" }, { "notes.txt", "world" } });

    auto second = m_cache.entriesFor(archivePath);
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value().size(), 2u);
}

TEST_F(ArchiveIndexCacheTest, FailsForMissingArchive)
{
    auto result = m_cache.entriesFor(m_tempDir / "does-not-exist.zip");

    EXPECT_TRUE(result.hasError());
}

// Regression test for the cache's std::deque being shared, unsynchronized mutable state: the real
// repository is called both from the UI thread and FilePreviewViewModel's background preview
// QThread, so entriesFor() must tolerate concurrent callers without corrupting m_entries.
TEST_F(ArchiveIndexCacheTest, EntriesForIsSafeUnderConcurrentAccess)
{
    const fs::path archivePath = m_tempDir / "sample.zip";
    ArchiveFixture::writeZip(archivePath, { { "readme.txt", "hello" }, { "notes.txt", "world" } });

    std::vector<std::thread> threads;
    std::atomic<int> failures{ 0 };
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([this, &archivePath, &failures]() {
            for (int call = 0; call < 20; ++call)
            {
                auto result = m_cache.entriesFor(archivePath);
                if (!result.hasValue() || result.value().size() != 2u)
                {
                    ++failures;
                }
            }
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(failures.load(), 0);
}
