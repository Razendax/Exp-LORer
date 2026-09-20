#include "CachingMediaDecoder.h"

#include "WindowsLongPath.h"

CachingMediaDecoder::CachingMediaDecoder(IMediaDecoder& inner, ThumbnailCache& cache)
    : m_inner(inner)
    , m_cache(cache)
{
}

Result<MediaMetadata> CachingMediaDecoder::extractMetadata(const std::filesystem::path& mediaFile) const
{
    return m_inner.extractMetadata(mediaFile);
}

Result<std::vector<std::byte>> CachingMediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                                        int maxHeight) const
{
    namespace fs = std::filesystem;

    const fs::path target = WindowsLongPath::withPrefix(mediaFile);

    std::error_code sizeEc;
    const std::uintmax_t size = fs::file_size(target, sizeEc);
    std::error_code timeEc;
    const auto mtime = fs::last_write_time(target, timeEc);

    if (sizeEc || timeEc)
    {
        // Can't build a reliable cache key -- fall through uncached; the inner decoder will
        // produce a proper Error for the caller.
        return m_inner.generateThumbnail(mediaFile, maxWidth, maxHeight);
    }

    const ThumbnailCache::Key key{ mediaFile, size, mtime.time_since_epoch().count(), maxWidth, maxHeight };

    if (auto cached = m_cache.get(key))
    {
        return Result<std::vector<std::byte>>::success(std::move(*cached));
    }

    auto generated = m_inner.generateThumbnail(mediaFile, maxWidth, maxHeight);
    if (generated)
    {
        m_cache.put(key, generated.value());
    }

    return generated;
}
