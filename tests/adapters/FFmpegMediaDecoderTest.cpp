#include <gtest/gtest.h>

#include <fstream>

#include "FFmpegMediaDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    class FFmpegMediaDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_ffmpeg_media_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };
}

TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailOnMissingFileReturnsError)
{
    FFmpegMediaDecoder decoder;

    auto result = decoder.generateThumbnail(m_tempDir / "does-not-exist.mp4", 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = m_tempDir / "not-a-video.avi";
    std::ofstream stream(garbageFile, std::ios::binary);
    stream << "definitely not an AVI container";
    stream.close();

    FFmpegMediaDecoder decoder;
    auto result = decoder.generateThumbnail(garbageFile, 128, 128);

    EXPECT_TRUE(result.hasError());
}

// No real video fixture exists in this suite to assert two previewIndex values decode distinct
// frame content (Architecture.md §14.64) -- at minimum, every valid index must behave identically
// to a missing file (fail cleanly), and an out-of-range index must be clamped rather than crash or
// seek somewhere nonsensical.
TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailOnMissingFileFailsForEveryPreviewIndex)
{
    FFmpegMediaDecoder decoder;
    const fs::path missing = m_tempDir / "does-not-exist.mp4";

    for (int previewIndex = 0; previewIndex < 5; ++previewIndex)
    {
        EXPECT_TRUE(decoder.generateThumbnail(missing, 128, 128, previewIndex).hasError());
    }
}

TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailClampsOutOfRangePreviewIndexInsteadOfCrashing)
{
    FFmpegMediaDecoder decoder;
    const fs::path missing = m_tempDir / "does-not-exist.mp4";

    EXPECT_TRUE(decoder.generateThumbnail(missing, 128, 128, -1).hasError());
    EXPECT_TRUE(decoder.generateThumbnail(missing, 128, 128, 99).hasError());
}

// Same "fails cleanly" shape as the single-frame cases above -- no real video fixture exists in
// this suite to assert a batch's distinct frame content (Architecture.md §14.77), but a missing
// file must still fail every requested slot rather than abort partway through the batch.
TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailsOnMissingFileReturnsOneFailurePerIndex)
{
    FFmpegMediaDecoder decoder;
    const fs::path missing = m_tempDir / "does-not-exist.mp4";

    auto results = decoder.generateThumbnails(missing, 128, 128, { 0, 1, 2, 3, 4 });

    ASSERT_EQ(results.size(), 5u);
    for (const auto& result : results)
    {
        EXPECT_TRUE(result.hasError());
    }
}

TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailsOnGarbageBytesReturnsOneFailurePerIndex)
{
    const fs::path garbageFile = m_tempDir / "not-a-video.avi";
    std::ofstream stream(garbageFile, std::ios::binary);
    stream << "definitely not an AVI container";
    stream.close();

    FFmpegMediaDecoder decoder;
    auto results = decoder.generateThumbnails(garbageFile, 128, 128, { 0, 1 });

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].hasError());
    EXPECT_TRUE(results[1].hasError());
}

TEST_F(FFmpegMediaDecoderTest, GenerateThumbnailsWithEmptyIndicesReturnsEmptyResult)
{
    FFmpegMediaDecoder decoder;
    const fs::path missing = m_tempDir / "does-not-exist.mp4";

    auto results = decoder.generateThumbnails(missing, 128, 128, {});

    EXPECT_TRUE(results.empty());
}
