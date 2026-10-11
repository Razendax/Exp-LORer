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

    Result<MediaThumbnail> generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                               int previewIndex = 0) const override;

    // Checks the cache for each requested previewIndex first, then calls the inner decoder's own
    // batched method exactly once for whatever indices missed (Architecture.md §14.77) -- an
    // all-cache-hit batch never touches the inner decoder at all, and a partially-warm batch still
    // only reopens the container once, for the misses. Results are merged back in the same order
    // as previewIndices.
    std::vector<Result<MediaThumbnail>> generateThumbnails(const std::filesystem::path& mediaFile, int maxWidth, int maxHeight,
                                                              const std::vector<int>& previewIndices) const override;

private:
    IMediaDecoder& m_inner;
    ThumbnailCache& m_cache;
};
