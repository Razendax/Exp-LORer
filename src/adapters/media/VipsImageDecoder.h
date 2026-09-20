#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for image extensions via libvips (Architecture.md §14.25): decode,
// resize, and JPEG-encode entirely through libvips (VImage/vips_thumbnail_buffer/
// vips_jpegsave_buffer), per the product requirement to use libvips specifically rather than
// Qt's own image codecs. Stateless; libvips itself is process-global (vips_init runs once,
// lazily, on first use).
class VipsImageDecoder : public IMediaDecoder
{
public:
    Result<MediaMetadata> extractMetadata(const std::filesystem::path& mediaFile) const override;
    Result<std::vector<std::byte>> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                       int maxHeight) const override;
};
