#pragma once

#include "FFmpegMediaDecoder.h"
#include "IMediaDecoder.h"
#include "QtGifImageDecoder.h"
#include "QtSvgImageDecoder.h"
#include "VipsImageDecoder.h"

// The concrete IMediaDecoder registered in CompositionRoot (Architecture.md §14.25/§14.79/§14.81):
// dispatches by extension to VipsImageDecoder (raster images), QtSvgImageDecoder (".svg"),
// QtGifImageDecoder (".gif"), or FFmpegMediaDecoder (video). None of the four backends call into
// each other, keeping them independently swappable.
class MediaDecoder : public IMediaDecoder
{
public:
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;

private:
    VipsImageDecoder m_imageDecoder;
    QtSvgImageDecoder m_svgDecoder;
    QtGifImageDecoder m_gifDecoder;
    FFmpegMediaDecoder m_videoDecoder;
};
