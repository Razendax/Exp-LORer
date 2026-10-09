#pragma once

#include "IMediaDecoder.h"
#include "ThumbnailCache.h"

// Decorator implementing IMediaDecoder: wraps an inner IMediaDecoder + ThumbnailCache
// (Architecture.md §8, §14.25). generateThumbnail() checks the cache first and populates it on a
// miss; the cached entry holds both the thumbnail bytes and the metadata produced alongside them
// (Architecture.md §14.63), so a cache hit never needs to fall back into the inner decoder at all.
// This is what CompositionRoot injects everywhere IMediaDecoder is needed -- caching stays
// transparent to callers such as FilePreviewUseCase/MediaProcessingUseCase.
class CachingMediaDecoder : public IMediaDecoder
{
public:
    CachingMediaDecoder(IMediaDecoder& inner, ThumbnailCache& cache);

    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                               int maxHeight) const override;

private:
    IMediaDecoder& m_inner;
    ThumbnailCache& m_cache;
};
