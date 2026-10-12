#include "QtSvgImageDecoder.h"

#include <chrono>
#include <cstring>
#include <fstream>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include "Logging.h"
#include "PathUtf8.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    // Mirrors VipsImageDecoder's readWholeFile: QSvgRenderer is handed raw bytes rather than a
    // filename so the same Windows-long-path-safe file-reading path is used for every decoder, and
    // QSvgRenderer never sees a raw possibly-non-ASCII path string.
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

    // Used when the SVG declares no width/height/viewBox, so QSvgRenderer::defaultSize() comes
    // back empty.
    constexpr int kFallbackNativeSize = 300;
}

Result<MediaThumbnail> QtSvgImageDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                               int maxHeight, int /*previewIndex*/) const
{
    auto bytesResult = readWholeFile(mediaFile);
    if (!bytesResult)
    {
        Logging::warn("QtSvgImageDecoder::generateThumbnail failed: " + std::string(toLogString(bytesResult.error().code)));
        return Result<MediaThumbnail>::failure(bytesResult.error());
    }
    const std::vector<std::byte>& sourceBytes = bytesResult.value();

    const QByteArray svgData(reinterpret_cast<const char*>(sourceBytes.data()), static_cast<int>(sourceBytes.size()));

    QSvgRenderer renderer;
    if (!renderer.load(svgData) || !renderer.isValid())
    {
        Logging::warn("QtSvgImageDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to parse SVG: " + PathUtf8::toUtf8(mediaFile)));
    }

    QSize nativeSize = renderer.defaultSize();
    if (nativeSize.width() <= 0 || nativeSize.height() <= 0)
    {
        nativeSize = QSize(kFallbackNativeSize, kFallbackNativeSize);
    }

    // Unlike VipsImageDecoder's "never upsize" rule, vector art has no native pixel ceiling, so
    // this always renders at the full box requested by the caller (fit inside maxWidth x
    // maxHeight, aspect preserved).
    const QSize targetSize = nativeSize.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio);

    QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter, QRectF(0, 0, targetSize.width(), targetSize.height()));
    painter.end();

    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    pngBuffer.open(QIODevice::WriteOnly);
    if (!image.save(&pngBuffer, "PNG"))
    {
        Logging::warn("QtSvgImageDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to PNG-encode the rendered SVG for " + PathUtf8::toUtf8(mediaFile)));
    }

    MediaMetadata metadata;
    if (auto created = MediaMetadata::create(nativeSize.width(), nativeSize.height(), std::chrono::milliseconds(0), "svg"))
    {
        metadata = std::move(created).value();
    }

    std::vector<std::byte> encoded(static_cast<std::size_t>(pngData.size()));
    std::memcpy(encoded.data(), pngData.constData(), encoded.size());

    Logging::debug("QtSvgImageDecoder::generateThumbnail succeeded: " + std::to_string(encoded.size()) + " byte(s)");
    return Result<MediaThumbnail>::success(MediaThumbnail{ std::move(encoded), std::move(metadata) });
}
