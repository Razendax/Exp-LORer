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

TEST_F(FFmpegMediaDecoderTest, ExtractMetadataOnMissingFileReturnsError)
{
    FFmpegMediaDecoder decoder;

    auto result = decoder.extractMetadata(m_tempDir / "does-not-exist.mp4");

    EXPECT_TRUE(result.hasError());
}

TEST_F(FFmpegMediaDecoderTest, ExtractMetadataOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = m_tempDir / "not-a-video.mp4";
    std::ofstream stream(garbageFile, std::ios::binary);
    stream << "this is not a real video container, just some bytes";
    stream.close();

    FFmpegMediaDecoder decoder;
    auto result = decoder.extractMetadata(garbageFile);

    EXPECT_TRUE(result.hasError());
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
