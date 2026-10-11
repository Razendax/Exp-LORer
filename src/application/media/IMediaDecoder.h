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

    // Batched form of generateThumbnail() for multiple previewIndices against the same mediaFile
    // (Architecture.md §14.77), one result per entry of previewIndices in the same order. The
    // default implementation simply loops generateThumbnail() per index -- correct for any
    // decoder, but only a decoder with real open/probe cost to amortize across frames
    // (FFmpegMediaDecoder) needs to override this for batching to actually help.
    virtual std::vector<Result<MediaThumbnail>> generateThumbnails(const std::filesystem::path& mediaFile,
                                                                     int maxWidth,
                                                                     int maxHeight,
                                                                     const std::vector<int>& previewIndices) const;
};
