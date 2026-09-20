#include "VipsImageDecoder.h"

#include <cstring>
#include <fstream>

#include <vips/vips.h>

#include "PathUtf8.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    // Called once per process, lazily, on first use (function-local static init is
    // thread-safe/exactly-once in C++11+, so no separate synchronization is needed here).
    void ensureVipsInitialized()
    {
        static const int result = vips_init("Exp-LORer");
        (void)result;
    }

    // libvips is handed raw bytes rather than a filename so its own filename-encoding rules
    // (and any Windows long-path caveats) never come into play -- the same file-reading path
    // this codebase already trusts elsewhere (StandardFileSystemRepository).
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

Result<MediaMetadata> VipsImageDecoder::extractMetadata(const std::filesystem::path& mediaFile) const
{
    ensureVipsInitialized();

    auto bytesResult = readWholeFile(mediaFile);
    if (!bytesResult)
    {
        return Result<MediaMetadata>::failure(bytesResult.error());
    }
    const std::vector<std::byte>& bytes = bytesResult.value();

    VipsImage* image = vips_image_new_from_buffer(bytes.data(), bytes.size(), "", nullptr);
    if (image == nullptr)
    {
        return Result<MediaMetadata>::failure(
            Error(ErrorCode::IoError, "libvips failed to decode " + PathUtf8::toUtf8(mediaFile)));
    }

    const int width = vips_image_get_width(image);
    const int height = vips_image_get_height(image);
    g_object_unref(image);

    return MediaMetadata::create(width, height, std::chrono::milliseconds(0), "image");
}

Result<std::vector<std::byte>> VipsImageDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                                     int maxHeight) const
{
    ensureVipsInitialized();

    auto bytesResult = readWholeFile(mediaFile);
    if (!bytesResult)
    {
        return Result<std::vector<std::byte>>::failure(bytesResult.error());
    }
    const std::vector<std::byte>& sourceBytes = bytesResult.value();

    VipsImage* thumbnail = nullptr;
    const int thumbnailRc = vips_thumbnail_buffer(const_cast<std::byte*>(sourceBytes.data()), sourceBytes.size(),
                                                    &thumbnail, maxWidth, "height", maxHeight, "size", VIPS_SIZE_DOWN,
                                                    nullptr);
    if (thumbnailRc != 0 || thumbnail == nullptr)
    {
        return Result<std::vector<std::byte>>::failure(
            Error(ErrorCode::IoError, "libvips failed to generate a thumbnail for " + PathUtf8::toUtf8(mediaFile)));
    }

    void* jpegBuffer = nullptr;
    std::size_t jpegLength = 0;
    const int saveRc = vips_jpegsave_buffer(thumbnail, &jpegBuffer, &jpegLength, "Q", 85, nullptr);
    g_object_unref(thumbnail);

    if (saveRc != 0 || jpegBuffer == nullptr)
    {
        return Result<std::vector<std::byte>>::failure(
            Error(ErrorCode::IoError, "libvips failed to JPEG-encode the thumbnail for " + PathUtf8::toUtf8(mediaFile)));
    }

    std::vector<std::byte> encoded(jpegLength);
    std::memcpy(encoded.data(), jpegBuffer, jpegLength);
    g_free(jpegBuffer);

    return Result<std::vector<std::byte>>::success(std::move(encoded));
}
