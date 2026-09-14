#include "MediaMetadata.h"

Result<MediaMetadata> MediaMetadata::create(int width, int height, std::chrono::milliseconds duration, std::string codec)
{
    if (width <= 0 || height <= 0)
    {
        return Result<MediaMetadata>::failure(
            Error(ErrorCode::InvalidArgument, "MediaMetadata width/height must be positive"));
    }

    if (duration < std::chrono::milliseconds::zero())
    {
        return Result<MediaMetadata>::failure(
            Error(ErrorCode::InvalidArgument, "MediaMetadata duration must not be negative"));
    }

    return Result<MediaMetadata>::success(MediaMetadata(width, height, duration, std::move(codec)));
}

MediaMetadata::MediaMetadata(int width, int height, std::chrono::milliseconds duration, std::string codec)
    : m_width(width)
    , m_height(height)
    , m_duration(duration)
    , m_codec(std::move(codec))
{
}

bool operator==(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept
{
    return lhs.m_width == rhs.m_width && lhs.m_height == rhs.m_height && lhs.m_duration == rhs.m_duration
        && lhs.m_codec == rhs.m_codec;
}
