#pragma once

#include "IMediaDecoder.h"

// Implements IMediaDecoder for ".pdf" (Architecture.md §14.101) via poppler-cpp, entirely
// isolated from the other decoders in this directory the same way QtSvgImageDecoder is -- nothing
// outside this class ever touches poppler directly. Stateless.
class PopplerPdfDecoder : public IMediaDecoder
{
public:
    // previewIndex selects which page to render (clamped into the document's valid page range);
    // defaults to 0, the first page. Wired through for forward compatibility with page
    // navigation, which is out of scope for this pass (Architecture.md §14.101).
    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;
};
