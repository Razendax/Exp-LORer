#include <gtest/gtest.h>

#include <fstream>

#include "MediaDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    class MediaDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_media_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };
}

// ".svg" also satisfies MediaExtensions::isImageExtension, so this confirms MediaDecoder checks
// isVectorImageExtension first and actually dispatches to QtSvgImageDecoder rather than
// VipsImageDecoder (Architecture.md §14.79): a decoder mismatch here would either fail outright
// or produce a codec other than "svg".
TEST_F(MediaDecoderTest, GenerateThumbnailDispatchesSvgToSvgDecoder)
{
    const fs::path svgFile = m_tempDir / "image.svg";
    std::ofstream stream(svgFile, std::ios::binary);
    stream << R"(<svg xmlns="http://www.w3.org/2000/svg" width="50" height="50"><circle cx="25" cy="25" r="20"/></svg>)";
    stream.close();

    MediaDecoder decoder;
    auto result = decoder.generateThumbnail(svgFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().metadata.codec(), "svg");
}

// ".gif" also satisfies MediaExtensions::isImageExtension, so this confirms MediaDecoder checks
// isAnimatedImageExtension first and actually dispatches to QtGifImageDecoder rather than
// VipsImageDecoder (Architecture.md §14.8x) -- this vcpkg build's libvips has no GIF loader
// enabled, so a decoder mismatch here would fail outright rather than just producing the wrong
// codec.
TEST_F(MediaDecoderTest, GenerateThumbnailDispatchesGifToGifDecoder)
{
    // The well-known 35-byte "smallest possible GIF" -- a single 1x1 transparent frame.
    const unsigned char kTinyGif[] = { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2C,
                                        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
                                        0x3B };

    const fs::path gifFile = m_tempDir / "image.gif";
    std::ofstream stream(gifFile, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(kTinyGif), sizeof(kTinyGif));
    stream.close();

    MediaDecoder decoder;
    auto result = decoder.generateThumbnail(gifFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().metadata.codec(), "gif");
}

// Confirms MediaDecoder checks isPdfExtension and actually dispatches to PopplerPdfDecoder
// (Architecture.md §14.101) rather than failing with "Unsupported media extension".
TEST_F(MediaDecoderTest, GenerateThumbnailDispatchesPdfToPdfDecoder)
{
    const char* kMinimalSinglePagePdf =
        "%PDF-1.1\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources << /Font << /F1 4 0 R >> >> "
        "/Contents 5 0 R >>\nendobj\n"
        "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
        "5 0 obj\n<< /Length 44 >>\nstream\n"
        "BT /F1 18 Tf 10 50 Td (Hello, world!) Tj ET\n"
        "endstream\nendobj\n"
        "xref\n0 6\n0000000000 65535 f \n"
        "trailer\n<< /Size 6 /Root 1 0 R >>\n"
        "startxref\n0\n"
        "%%EOF";

    const fs::path pdfFile = m_tempDir / "doc.pdf";
    std::ofstream stream(pdfFile, std::ios::binary);
    stream << kMinimalSinglePagePdf;
    stream.close();

    MediaDecoder decoder;
    auto result = decoder.generateThumbnail(pdfFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().metadata.codec(), "pdf");
}
