#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "IMediaDecoder.h"
#include "MediaMetadata.h"
#include "Result.h"

// Manages extraction of thumbnails and basic metadata for images and videos
// (Architecture.md §2.2). Stateless: a single shared instance may serve concurrent callers
// (Architecture.md §14.2).
class MediaProcessingUseCase
{
public:
    explicit MediaProcessingUseCase(IMediaDecoder& mediaDecoder);

    Result<MediaMetadata> extractMetadata(const std::filesystem::path& mediaFile) const;
    Result<std::vector<std::byte>> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight) const;

private:
    IMediaDecoder& m_mediaDecoder;
};
