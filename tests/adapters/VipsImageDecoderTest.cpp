#include <gtest/gtest.h>

#include <fstream>

#include "VipsImageDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    class VipsImageDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_vips_image_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };
}

TEST_F(VipsImageDecoderTest, ExtractMetadataOnMissingFileReturnsError)
{
    VipsImageDecoder decoder;

    auto result = decoder.extractMetadata(m_tempDir / "does-not-exist.jpg");

    EXPECT_TRUE(result.hasError());
}

TEST_F(VipsImageDecoderTest, ExtractMetadataOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = m_tempDir / "not-an-image.jpg";
    std::ofstream stream(garbageFile, std::ios::binary);
    stream << "this is not a real image file, just some bytes";
    stream.close();

    VipsImageDecoder decoder;
    auto result = decoder.extractMetadata(garbageFile);

    EXPECT_TRUE(result.hasError());
}

TEST_F(VipsImageDecoderTest, GenerateThumbnailOnMissingFileReturnsError)
{
    VipsImageDecoder decoder;

    auto result = decoder.generateThumbnail(m_tempDir / "does-not-exist.jpg", 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(VipsImageDecoderTest, GenerateThumbnailOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = m_tempDir / "not-an-image.png";
    std::ofstream stream(garbageFile, std::ios::binary);
    stream << "definitely not a PNG";
    stream.close();

    VipsImageDecoder decoder;
    auto result = decoder.generateThumbnail(garbageFile, 64, 64);

    EXPECT_TRUE(result.hasError());
}
