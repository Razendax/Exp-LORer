#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "MediaMetadata.h"
#include "Result.h"

// Port for decoding media streams and extracting metadata/thumbnails (Architecture.md §2.2).
// Implemented by src/adapters/media/QtMediaDecoder or FFmpegMediaDecoder.
class IMediaDecoder
{
public:
    virtual ~IMediaDecoder() = default;

    virtual Result<MediaMetadata> extractMetadata(const std::filesystem::path& mediaFile) const = 0;

    // Encoded (JPEG/WebP) thumbnail bytes, scaled to fit within maxWidth x maxHeight.
    virtual Result<std::vector<std::byte>> generateThumbnail(const std::filesystem::path& mediaFile,
                                                              int maxWidth,
                                                              int maxHeight) const = 0;
};
