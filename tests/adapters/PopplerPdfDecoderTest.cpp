#include <gtest/gtest.h>

#include <fstream>

#include "PopplerPdfDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    // The well-known minimal single-page "hello world" PDF -- plain ASCII PDF syntax, no
    // compression, no external fixture file, same posture as QtSvgImageDecoderTest's inline SVG
    // string literals.
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

    class PopplerPdfDecoderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_poppler_pdf_decoder_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path writePdf(const std::string& contents, const std::string& fileName = "doc.pdf")
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

TEST_F(PopplerPdfDecoderTest, GenerateThumbnailOnMissingFileReturnsError)
{
    PopplerPdfDecoder decoder;

    auto result = decoder.generateThumbnail(m_tempDir / "does-not-exist.pdf", 128, 128);

    EXPECT_TRUE(result.hasError());
}

TEST_F(PopplerPdfDecoderTest, GenerateThumbnailOnGarbageBytesReturnsError)
{
    const fs::path garbageFile = writePdf("definitely not a PDF", "not-a-pdf.pdf");

    PopplerPdfDecoder decoder;
    auto result = decoder.generateThumbnail(garbageFile, 64, 64);

    EXPECT_TRUE(result.hasError());
}

TEST_F(PopplerPdfDecoderTest, GenerateThumbnailOnValidPdfReturnsPngBytesAndPageCount)
{
    const fs::path pdfFile = writePdf(kMinimalSinglePagePdf);

    PopplerPdfDecoder decoder;
    auto result = decoder.generateThumbnail(pdfFile, 64, 64);

    ASSERT_TRUE(result.hasValue());
    const MediaThumbnail& thumbnail = result.value();
    EXPECT_EQ(thumbnail.metadata.codec(), "pdf");
    EXPECT_EQ(thumbnail.metadata.pageCount(), 1);
    EXPECT_GT(thumbnail.metadata.width(), 0);
    EXPECT_GT(thumbnail.metadata.height(), 0);

    // PNG signature bytes.
    ASSERT_GE(thumbnail.bytes.size(), std::size_t(8));
    const unsigned char kPngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (std::size_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(thumbnail.bytes[i]), kPngSignature[i]);
    }
}
