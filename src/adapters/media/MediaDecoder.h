#pragma once

#include "FFmpegMediaDecoder.h"
#include "IMediaDecoder.h"
#include "VipsImageDecoder.h"

// The concrete IMediaDecoder registered in CompositionRoot (Architecture.md §14.25): dispatches
// by extension to VipsImageDecoder (images) or FFmpegMediaDecoder (video). Neither backend calls
// into the other, keeping the two independently swappable.
class MediaDecoder : public IMediaDecoder
{
public:
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;

private:
    VipsImageDecoder m_imageDecoder;
    FFmpegMediaDecoder m_videoDecoder;
};
