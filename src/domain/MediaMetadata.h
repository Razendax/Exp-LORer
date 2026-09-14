#pragma once

#include <chrono>
#include <string>

#include "Result.h"

// Entity representing media properties extracted from an image/video file.
class MediaMetadata
{
public:
    static Result<MediaMetadata> create(int width, int height, std::chrono::milliseconds duration, std::string codec);

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    std::chrono::milliseconds duration() const noexcept { return m_duration; }
    const std::string& codec() const noexcept { return m_codec; }

    friend bool operator==(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept;

private:
    MediaMetadata(int width, int height, std::chrono::milliseconds duration, std::string codec);

    int m_width;
    int m_height;
    std::chrono::milliseconds m_duration;
    std::string m_codec;
};

inline bool operator!=(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept
{
    return !(lhs == rhs);
}
