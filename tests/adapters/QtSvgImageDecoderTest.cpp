#include <gtest/gtest.h>

#include <fstream>

#include "QtSvgImageDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    class QtSvgImageDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_qt_svg_image_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path writeSvg(const std::string& contents, const std::string& fileName = "image.svg")
        {
            const fs::path file = m_tempDir / fileName;
            std::ofstream stream(file, std::ios::binary);
            stream << contents;
            stream.close();
            return file;
        }

        fs::path m_tempDir;
    };
}

TEST_F(QtSvgImageDecoderTest, GenerateThumbnailOnMissingFileReturnsError)
{
    QtSvgImageDecoder decoder;

    auto result = decoder.generateThumbnail(m_tempDir / "does-not-exist.svg", 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(QtSvgImageDecoderTest, GenerateThumbnailOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = writeSvg("definitely not an SVG", "not-an-image.svg");

    QtSvgImageDecoder decoder;
    auto result = decoder.generateThumbnail(garbageFile, 64, 64);

    EXPECT_TRUE(result.hasError());
}

TEST_F(QtSvgImageDecoderTest, GenerateThumbnailOnValidSvgReturnsPngBytesAndNativeSize)
{
    const fs::path svgFile = writeSvg(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100"><rect width="200" height="100" fill="red"/></svg>)");

    QtSvgImageDecoder decoder;
    auto result = decoder.generateThumbnail(svgFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    const MediaThumbnail& thumbnail = result.value();
    EXPECT_EQ(thumbnail.metadata.width(), 200);
    EXPECT_EQ(thumbnail.metadata.height(), 100);
    EXPECT_EQ(thumbnail.metadata.codec(), "svg");

    // PNG signature bytes -- confirms SVG rendering is PNG-encoded (keeps alpha), not JPEG.
    ASSERT_GE(thumbnail.bytes.size(), std::size_t(8));
    const unsigned char kPngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (std::size_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(thumbnail.bytes[i]), kPngSignature[i]);
    }
}
