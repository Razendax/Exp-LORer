#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "Result.h"

// Entity representing media properties extracted from an image/video file.
class MediaMetadata
{
public:
    // Zero-valued (width/height 0, empty codec) -- used by FilePreview for non-Image/Video kinds
    // and as the graceful-degradation fallback when extraction fails.
    MediaMetadata() noexcept;

    static Result<MediaMetadata> create(int width, int height, std::chrono::milliseconds duration, std::string codec,
                                         std::int64_t bitRateBps = 0, int pageCount = 0);

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    std::chrono::milliseconds duration() const noexcept { return m_duration; }
    const std::string& codec() const noexcept { return m_codec; }
    // Bits per second; 0 = unknown/not applicable (e.g. still images).
    std::int64_t bitRateBps() const noexcept { return m_bitRateBps; }
    // PDF only; 0 for every other kind (Architecture.md §14.101).
    int pageCount() const noexcept { return m_pageCount; }

    friend bool operator==(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept;

private:
    MediaMetadata(int width, int height, std::chrono::milliseconds duration, std::string codec, std::int64_t bitRateBps,
                  int pageCount);

    int m_width;
    int m_height;
    std::chrono::milliseconds m_duration;
    std::string m_codec;
    std::int64_t m_bitRateBps;
    int m_pageCount;
};

inline bool operator!=(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept
{
    return !(lhs == rhs);
}
