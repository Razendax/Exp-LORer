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

    // Classic 8px checkerboard used by image editors (Photoshop, GIMP, etc.) to depict
    // transparency, light gray/white so it stays visible against both light and dark app themes.
    constexpr int kCheckerSquarePx = 8;
    constexpr unsigned char kCheckerLight = 255;
    constexpr unsigned char kCheckerDark = 205;

    // Pixel data for one 2x2-square tile; vips_replicate below tessellates it to any image size.
    std::vector<unsigned char> buildCheckerboardTilePixels(int tileSize)
    {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(tileSize) * tileSize * 3);
        for (int y = 0; y < tileSize; ++y)
        {
            for (int x = 0; x < tileSize; ++x)
            {
                const bool lightSquare = ((x / kCheckerSquarePx) + (y / kCheckerSquarePx)) % 2 == 0;
                const unsigned char value = lightSquare ? kCheckerLight : kCheckerDark;
                unsigned char* pixel = &pixels[(static_cast<std::size_t>(y) * tileSize + x) * 3];
                pixel[0] = value;
                pixel[1] = value;
                pixel[2] = value;
            }
        }
        return pixels;
    }

    // Composites `source` (which must have an alpha channel) over a tiled checkerboard the size
    // of `source`. Without this, vips_jpegsave_buffer would flatten transparent pixels onto solid
    // black (JPEG has no alpha channel), which makes any image with a dark, transparent-backed
    // subject disappear into the preview panel. Returns nullptr on failure; never unrefs `source`.
    VipsImage* compositeOntoCheckerboard(VipsImage* source)
    {
        const int tileSize = kCheckerSquarePx * 2;
        const std::vector<unsigned char> tilePixels = buildCheckerboardTilePixels(tileSize);

        VipsImage* tile = vips_image_new_from_memory_copy(tilePixels.data(), tilePixels.size(), tileSize, tileSize, 3,
                                                            VIPS_FORMAT_UCHAR);
        if (tile == nullptr)
        {
            return nullptr;
        }

        VipsImage* srgbTile = nullptr;
        if (vips_copy(tile, &srgbTile, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr) != 0)
        {
            g_object_unref(tile);
            return nullptr;
        }
        g_object_unref(tile);

        const int across = (source->Xsize + tileSize - 1) / tileSize;
        const int down = (source->Ysize + tileSize - 1) / tileSize;

        VipsImage* tiled = nullptr;
        if (vips_replicate(srgbTile, &tiled, across, down, nullptr) != 0)
        {
            g_object_unref(srgbTile);
            return nullptr;
        }
        g_object_unref(srgbTile);

        VipsImage* background = nullptr;
        const int cropRc = vips_crop(tiled, &background, 0, 0, source->Xsize, source->Ysize, nullptr);
        g_object_unref(tiled);
        if (cropRc != 0)
        {
            return nullptr;
        }

        VipsImage* composited = nullptr;
        const int compositeRc = vips_composite2(background, source, &composited, VIPS_BLEND_MODE_OVER, nullptr);
        g_object_unref(background);
        if (compositeRc != 0)
        {
            return nullptr;
        }

        return composited;
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

    VipsImage* imageToEncode = thumbnail;
    VipsImage* checkeredThumbnail = nullptr;
    if (vips_image_hasalpha(thumbnail))
    {
        checkeredThumbnail = compositeOntoCheckerboard(thumbnail);
        if (checkeredThumbnail == nullptr)
        {
            g_object_unref(thumbnail);
            return Result<std::vector<std::byte>>::failure(Error(
                ErrorCode::IoError, "libvips failed to composite the transparency checkerboard for " + PathUtf8::toUtf8(mediaFile)));
        }
        imageToEncode = checkeredThumbnail;
    }

    void* jpegBuffer = nullptr;
    std::size_t jpegLength = 0;
    const int saveRc = vips_jpegsave_buffer(imageToEncode, &jpegBuffer, &jpegLength, "Q", 85, nullptr);
    if (checkeredThumbnail != nullptr)
    {
        g_object_unref(checkeredThumbnail);
    }
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
