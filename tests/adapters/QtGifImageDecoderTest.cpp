#include <gtest/gtest.h>

#include <fstream>

#include "QtGifImageDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    // The well-known 35-byte "smallest possible GIF" -- a single 1x1 transparent frame.
    constexpr unsigned char kTinyGif[] = { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2C,
                                            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
                                            0x3B };

    class QtGifImageDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_qt_gif_image_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path writeFile(const char* data, std::size_t size, const std::string& fileName)
        {
            const fs::path file = m_tempDir / fileName;
            std::ofstream stream(file, std::ios::binary);
            stream.write(data, static_cast<std::streamsize>(size));
            stream.close();
            return file;
        }

        fs::path m_tempDir;
    };
}

TEST_F(QtGifImageDecoderTest, GenerateThumbnailOnMissingFileReturnsError)
{
    QtGifImageDecoder decoder;

    auto result = decoder.generateThumbnail(m_tempDir / "does-not-exist.gif", 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(QtGifImageDecoderTest, GenerateThumbnailOnGarbageBytesReturnsError)
{
    const char garbage[] = "definitely not a gif";
    const fs::path garbageFile = writeFile(garbage, sizeof(garbage) - 1, "not-an-image.gif");

    QtGifImageDecoder decoder;
    auto result = decoder.generateThumbnail(garbageFile, 64, 64);

    EXPECT_TRUE(result.hasError());
}

TEST_F(QtGifImageDecoderTest, GenerateThumbnailOnValidGifReturnsPngBytesAndNativeSize)
{
    const fs::path gifFile =
        writeFile(reinterpret_cast<const char*>(kTinyGif), sizeof(kTinyGif), "image.gif");

    QtGifImageDecoder decoder;
    auto result = decoder.generateThumbnail(gifFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    const MediaThumbnail& thumbnail = result.value();
    EXPECT_EQ(thumbnail.metadata.width(), 1);
    EXPECT_EQ(thumbnail.metadata.height(), 1);
    EXPECT_EQ(thumbnail.metadata.codec(), "gif");

    // PNG signature bytes -- confirms the decoded frame is PNG-encoded (keeps alpha), not JPEG.
    ASSERT_GE(thumbnail.bytes.size(), std::size_t(8));
    const unsigned char kPngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (std::size_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(thumbnail.bytes[i]), kPngSignature[i]);
    }
}
