#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for ".svg" (Architecture.md §14.79) via QSvgRenderer, entirely
// isolated from VipsImageDecoder/FFmpegMediaDecoder the same way those two are isolated from each
// other -- nothing outside this class ever touches QSvgRenderer directly, so swapping to another
// SVG library later means writing one new class with this same shape. Stateless.
class QtSvgImageDecoder : public IMediaDecoder
{
public:
    // previewIndex is accepted and ignored -- vector images have no concept of a time position,
    // same posture as VipsImageDecoder.
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;
};
