#include "PopplerPdfDecoder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>

#include <poppler-document.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <poppler-rectangle.h>

#include <QBuffer>
#include <QByteArray>
#include <QImage>

#include "Logging.h"
#include "PathUtf8.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    // Mirrors VipsImageDecoder/QtSvgImageDecoder's readWholeFile: poppler is handed raw bytes
    // rather than a filename so the same Windows-long-path-safe file-reading path is used for
    // every decoder, and poppler never sees a raw possibly-non-ASCII path string.
    Result<std::vector<std::byte>> readWholeFile(const fs::path& path)
    {
        std::ifstream stream(WindowsLongPath::withPrefix(path), std::ios::binary);
        if (!stream)
        {
            return Result<std::vector<std::byte>>::failure(
                Error(ErrorCode::IoError, "Failed to open " + PathUtf8::toUtf8(path) + " for reading"));
        }

        stream.seekg(0, std::ios::end);
        const auto size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        if (size <= 0)
        {
            return Result<std::vector<std::byte>>::failure(
                Error(ErrorCode::IoError, "Empty or unreadable file: " + PathUtf8::toUtf8(path)));
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(buffer.data()), size);
        return Result<std::vector<std::byte>>::success(std::move(buffer));
    }

    // poppler::page_renderer::render_page()'s xres/yres args are a DPI, while page_rect() reports
    // page size in points (1/72 inch) -- this converts between the two.
    constexpr double kPointsPerInch = 72.0;
}

Result<MediaThumbnail> PopplerPdfDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                               int maxHeight, int previewIndex) const
{
    auto bytesResult = readWholeFile(mediaFile);
    if (!bytesResult)
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(bytesResult.error().code)));
        return Result<MediaThumbnail>::failure(bytesResult.error());
    }
    const std::vector<std::byte>& sourceBytes = bytesResult.value();

    std::unique_ptr<poppler::document> document(poppler::document::load_from_raw_data(
        reinterpret_cast<const char*>(sourceBytes.data()), static_cast<int>(sourceBytes.size())));
    if (!document || document->is_locked())
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(Error(ErrorCode::IoError, "Failed to load PDF: " + PathUtf8::toUtf8(mediaFile)));
    }

    const int pageCount = document->pages();
    if (pageCount <= 0)
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(Error(ErrorCode::IoError, "PDF has no pages: " + PathUtf8::toUtf8(mediaFile)));
    }

    const int pageIndex = std::clamp(previewIndex, 0, pageCount - 1);
    std::unique_ptr<poppler::page> page(document->create_page(pageIndex));
    if (!page)
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to load PDF page: " + PathUtf8::toUtf8(mediaFile)));
    }

    const poppler::rectf pageRect = page->page_rect();
    if (pageRect.width() <= 0 || pageRect.height() <= 0)
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "PDF page has invalid dimensions: " + PathUtf8::toUtf8(mediaFile)));
    }

    // Render directly at the target box (no large-then-downscale pass), same "fit inside
    // maxWidth x maxHeight, aspect preserved" reasoning QtSvgImageDecoder already uses for vector
    // content: compute the DPI that fits the page inside the requested box.
    const double dpi = std::min(maxWidth * kPointsPerInch / pageRect.width(), maxHeight * kPointsPerInch / pageRect.height());

    poppler::page_renderer renderer;
    renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
    renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);

    const poppler::image rendered = renderer.render_page(page.get(), dpi, dpi);
    if (!rendered.is_valid())
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to render PDF page: " + PathUtf8::toUtf8(mediaFile)));
    }

    // .copy() to own the pixel data -- `rendered`'s buffer is freed once it goes out of scope.
    const QImage image = QImage(reinterpret_cast<const uchar*>(rendered.const_data()), rendered.width(), rendered.height(),
                                  rendered.bytes_per_row(), QImage::Format_ARGB32_Premultiplied)
                              .copy();

    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    pngBuffer.open(QIODevice::WriteOnly);
    if (!image.save(&pngBuffer, "PNG"))
    {
        Logging::warn("PopplerPdfDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to PNG-encode the rendered PDF page for " + PathUtf8::toUtf8(mediaFile)));
    }

    MediaMetadata metadata;
    if (auto created = MediaMetadata::create(image.width(), image.height(), std::chrono::milliseconds(0), "pdf",
                                               /*bitRateBps*/ 0, pageCount))
    {
        metadata = std::move(created).value();
    }

    std::vector<std::byte> encoded(static_cast<std::size_t>(pngData.size()));
    std::memcpy(encoded.data(), pngData.constData(), encoded.size());

    Logging::debug("PopplerPdfDecoder::generateThumbnail succeeded: " + std::to_string(encoded.size()) + " byte(s)");
    return Result<MediaThumbnail>::success(MediaThumbnail{ std::move(encoded), std::move(metadata) });
}
