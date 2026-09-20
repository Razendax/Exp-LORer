#pragma once

#include "IMediaDecoder.h"
#include "ThumbnailCache.h"

// Decorator implementing IMediaDecoder: wraps an inner IMediaDecoder + ThumbnailCache
// (Architecture.md §8, §14.25). generateThumbnail() checks the cache first and populates it on a
// miss; extractMetadata() passes through uncached (cheap, not on the preview hot path). This is
// what CompositionRoot injects everywhere IMediaDecoder is needed -- caching stays transparent to
// callers such as FilePreviewUseCase/MediaProcessingUseCase.
class CachingMediaDecoder : public IMediaDecoder
{
public:
    CachingMediaDecoder(IMediaDecoder& inner, ThumbnailCache& cache);

    Result<MediaMetadata> extractMetadata(const std::filesystem::path& mediaFile) const override;
    Result<std::vector<std::byte>> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                       int maxHeight) const override;

private:
    IMediaDecoder& m_inner;
    ThumbnailCache& m_cache;
};
