#include "QtGifImageDecoder.h"

#include <chrono>
#include <cstring>
#include <fstream>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>

#include "Logging.h"
#include "PathUtf8.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    // Mirrors VipsImageDecoder/QtSvgImageDecoder's readWholeFile: the GIF bytes are handed to Qt
    // through a QBuffer rather than letting QImageReader open the path itself, so this decoder uses
    // the same Windows-long-path-safe file-reading path as every other decoder in this codebase.
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
}

Result<MediaThumbnail> QtGifImageDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                               int maxHeight, int /*previewIndex*/) const
{
    auto bytesResult = readWholeFile(mediaFile);
    if (!bytesResult)
    {
        Logging::warn("QtGifImageDecoder::generateThumbnail failed: " + std::string(toLogString(bytesResult.error().code)));
        return Result<MediaThumbnail>::failure(bytesResult.error());
    }
    const std::vector<std::byte>& sourceBytes = bytesResult.value();

    const QByteArray gifData(reinterpret_cast<const char*>(sourceBytes.data()), static_cast<int>(sourceBytes.size()));
    QBuffer buffer;
    buffer.setData(gifData);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer, "gif");
    const QImage firstFrame = reader.read();
    if (firstFrame.isNull())
    {
        Logging::warn("QtGifImageDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to decode GIF: " + PathUtf8::toUtf8(mediaFile)));
    }

    const QSize nativeSize = firstFrame.size();

    // Same "never upsize" rule as VipsImageDecoder's VIPS_SIZE_DOWN -- only shrink a frame larger
    // than the requested box, keeping its aspect ratio either way.
    QSize targetSize = nativeSize;
    if (nativeSize.width() > maxWidth || nativeSize.height() > maxHeight)
    {
        targetSize = nativeSize.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio);
    }

    const QImage scaledFrame =
        targetSize == nativeSize ? firstFrame : firstFrame.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    pngBuffer.open(QIODevice::WriteOnly);
    if (!scaledFrame.save(&pngBuffer, "PNG"))
    {
        Logging::warn("QtGifImageDecoder::generateThumbnail failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::IoError, "Failed to PNG-encode the decoded GIF for " + PathUtf8::toUtf8(mediaFile)));
    }

    MediaMetadata metadata;
    if (auto created = MediaMetadata::create(nativeSize.width(), nativeSize.height(), std::chrono::milliseconds(0), "gif"))
    {
        metadata = std::move(created).value();
    }

    std::vector<std::byte> encoded(static_cast<std::size_t>(pngData.size()));
    std::memcpy(encoded.data(), pngData.constData(), encoded.size());

    Logging::debug("QtGifImageDecoder::generateThumbnail succeeded: " + std::to_string(encoded.size()) + " byte(s)");
    return Result<MediaThumbnail>::success(MediaThumbnail{ std::move(encoded), std::move(metadata) });
}
