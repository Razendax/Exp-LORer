#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "MediaThumbnail.h"
#include "Result.h"

// Port for decoding media streams and extracting metadata/thumbnails (Architecture.md §2.2).
// Implemented by src/adapters/media/QtMediaDecoder or FFmpegMediaDecoder.
class IMediaDecoder
{
public:
    virtual ~IMediaDecoder() = default;

    // Encoded (JPEG/WebP) thumbnail bytes scaled to fit within maxWidth x maxHeight, plus the
    // source file's metadata -- both produced by the same decode pass (Architecture.md §14.63),
    // since every caller needs both and a separate metadata-only call would re-decode the file.
    // previewIndex (Architecture.md §14.64) is ignored by image decoders; for video, it selects
    // which of MediaPreviewFrames::kCount equally-spaced frames to decode.
    virtual Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile,
                                                       int maxWidth,
                                                       int maxHeight,
                                                       int previewIndex = 0) const = 0;
};
