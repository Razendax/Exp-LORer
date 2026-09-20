#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for video extensions via FFmpeg (Architecture.md §14.25): demuxes and
// decodes one representative frame (~10% into the stream, falling back to the first decodable
// frame when duration is unavailable), scales it via libswscale, and JPEG-encodes it via
// libavcodec's MJPEG encoder. Self-contained -- does not call into VipsImageDecoder, keeping the
// two backends independently swappable.
class FFmpegMediaDecoder : public IMediaDecoder
{
public:
    Result<MediaMetadata> extractMetadata(const std::filesystem::path& mediaFile) const override;
    Result<std::vector<std::byte>> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                       int maxHeight) const override;
};
