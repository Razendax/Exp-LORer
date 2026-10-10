#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for video extensions via FFmpeg (Architecture.md §14.25): demuxes and
// decodes one representative frame (one of MediaPreviewFrames::kCount equally-spaced positions,
// selected by previewIndex -- Architecture.md §14.64 -- falling back to the first decodable frame
// when duration is unavailable), scales it via libswscale, and JPEG-encodes it via libavcodec's
// MJPEG encoder. Self-contained -- does not call into VipsImageDecoder, keeping the two backends
// independently swappable.
class FFmpegMediaDecoder : public IMediaDecoder
{
public:
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;
};
