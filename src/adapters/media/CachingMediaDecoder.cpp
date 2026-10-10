#include "CachingMediaDecoder.h"

#include <cstdint>
#include <cstring>

#include "Logging.h"
#include "WindowsLongPath.h"

namespace
{
    // On-disk blob layout for one cached entry: a small fixed-size metadata header, the codec
    // name, then the raw encoded thumbnail bytes. ThumbnailCache itself stays a plain byte-blob
    // cache (Architecture.md §8) -- this (de)serialization is CachingMediaDecoder's own concern,
    // needed because a cache hit must be able to return MediaMetadata without re-decoding the
    // source file (Architecture.md §14.63).
    struct MetadataHeader
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int64_t durationMs = 0;
        std::int64_t bitRateBps = 0;
        std::uint32_t codecLength = 0;
    };

    std::vector<std::byte> serialize(const MediaThumbnail& thumbnail)
    {
        const MediaMetadata& metadata = thumbnail.metadata;
        const std::string& codec = metadata.codec();

        MetadataHeader header;
        header.width = metadata.width();
        header.height = metadata.height();
        header.durationMs = metadata.duration().count();
        header.bitRateBps = metadata.bitRateBps();
        header.codecLength = static_cast<std::uint32_t>(codec.size());

        std::vector<std::byte> blob(sizeof(MetadataHeader) + codec.size() + thumbnail.bytes.size());
        std::byte* cursor = blob.data();
        std::memcpy(cursor, &header, sizeof(MetadataHeader));
        cursor += sizeof(MetadataHeader);
        std::memcpy(cursor, codec.data(), codec.size());
        cursor += codec.size();
        std::memcpy(cursor, thumbnail.bytes.data(), thumbnail.bytes.size());

        return blob;
    }

    // Returns std::nullopt for anything that doesn't parse as a well-formed blob (truncated,
    // corrupted, or written by a since-changed format) -- the caller treats that exactly like a
    // cache miss and regenerates, rather than failing the whole request.
    std::optional<MediaThumbnail> deserialize(const std::vector<std::byte>& blob)
    {
        if (blob.size() < sizeof(MetadataHeader))
        {
            return std::nullopt;
        }

        MetadataHeader header;
        std::memcpy(&header, blob.data(), sizeof(MetadataHeader));

        const std::size_t headerAndCodecSize = sizeof(MetadataHeader) + header.codecLength;
        if (headerAndCodecSize > blob.size())
        {
            return std::nullopt;
        }

        std::string codec(reinterpret_cast<const char*>(blob.data() + sizeof(MetadataHeader)), header.codecLength);

        auto createdMetadata =
            MediaMetadata::create(header.width, header.height, std::chrono::milliseconds(header.durationMs),
                                   std::move(codec), header.bitRateBps);
        if (!createdMetadata)
        {
            return std::nullopt;
        }

        MediaThumbnail thumbnail;
        thumbnail.metadata = std::move(createdMetadata).value();
        thumbnail.bytes.assign(blob.begin() + static_cast<std::ptrdiff_t>(headerAndCodecSize), blob.end());
        return thumbnail;
    }
}

CachingMediaDecoder::CachingMediaDecoder(IMediaDecoder& inner, ThumbnailCache& cache)
    : m_inner(inner)
    , m_cache(cache)
{
}

Result<MediaThumbnail> CachingMediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                                 int maxHeight, int previewIndex) const
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
        return m_inner.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }

    const ThumbnailCache::Key key{ mediaFile, size, mtime.time_since_epoch().count(), maxWidth, maxHeight, previewIndex };

    if (auto cachedBlob = m_cache.get(key))
    {
        if (auto cached = deserialize(*cachedBlob))
        {
            Logging::debug("CachingMediaDecoder: thumbnail cache hit");
            return Result<MediaThumbnail>::success(std::move(*cached));
        }
        Logging::warn("CachingMediaDecoder: discarding unparsable cache entry, regenerating");
    }

    Logging::debug("CachingMediaDecoder: thumbnail cache miss");
    auto generated = m_inner.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    if (generated)
    {
        m_cache.put(key, serialize(generated.value()));
    }

    return generated;
}
