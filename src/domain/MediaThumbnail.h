#pragma once

#include <cstddef>
#include <vector>

#include "MediaMetadata.h"

// Result of IMediaDecoder::generateThumbnail(): the encoded thumbnail/poster-frame bytes plus the
// metadata produced by the same decode pass. The two are returned together because every known
// caller needs both, and extracting metadata as a separate call would mean re-decoding the source
// file a second time (Architecture.md §14.63).
struct MediaThumbnail
{
    std::vector<std::byte> bytes;
    MediaMetadata metadata;
};
