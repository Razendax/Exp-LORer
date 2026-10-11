#pragma once

#include <filesystem>
#include <vector>

#include "IMediaDecoder.h"
#include "MediaThumbnail.h"
#include "Result.h"

// Manages extraction of thumbnails and basic metadata for images and videos
// (Architecture.md §2.2). Stateless: a single shared instance may serve concurrent callers
// (Architecture.md §14.2).
class MediaProcessingUseCase
{
public:
    explicit MediaProcessingUseCase(IMediaDecoder& mediaDecoder);

    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                              int previewIndex = 0) const;

    // Batched form of generateThumbnail() (Architecture.md §14.77): validates maxWidth/maxHeight
    // and every previewIndex the same way generateThumbnail() does, then forwards to the decoder's
    // own batched method. An invalid call (bad dimensions, or any previewIndex out of range)
    // returns a same-sized vector of the InvalidArgument failure rather than forwarding a
    // partially-invalid batch.
    std::vector<Result<MediaThumbnail>> generateThumbnails(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                                              const std::vector<int>& previewIndices) const;

private:
    IMediaDecoder& m_mediaDecoder;
};
