#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for ".gif" via QImageReader/QImage rather than libvips -- this vcpkg
// build's libvips port (Architecture.md §14.81) never enables its built-in GIF loader (no "gif"
// feature exists to request it through vcpkg.json), so VipsImageDecoder silently fails to decode
// any GIF. QImageReader decodes GIF natively with no extra Qt component beyond QtGui. Entirely
// isolated from VipsImageDecoder/QtSvgImageDecoder/FFmpegMediaDecoder, the same way those three are
// isolated from each other. Stateless.
class QtGifImageDecoder : public IMediaDecoder
{
public:
    // previewIndex is accepted and ignored -- the thumbnail is always the GIF's first frame; full
    // animation playback goes through GifPlayerWidget/MediaHoverPreviewPopup via QMovie instead,
    // entirely separate from this decoder.
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;
};
