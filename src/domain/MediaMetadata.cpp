#include "MediaMetadata.h"

MediaMetadata::MediaMetadata() noexcept
    : m_width(0)
    , m_height(0)
    , m_duration(0)
    , m_bitRateBps(0)
{
}

Result<MediaMetadata> MediaMetadata::create(int width, int height, std::chrono::milliseconds duration, std::string codec,
                                             std::int64_t bitRateBps)
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

    return Result<MediaMetadata>::success(MediaMetadata(width, height, duration, std::move(codec), bitRateBps));
}

MediaMetadata::MediaMetadata(int width, int height, std::chrono::milliseconds duration, std::string codec, std::int64_t bitRateBps)
    : m_width(width)
    , m_height(height)
    , m_duration(duration)
    , m_codec(std::move(codec))
    , m_bitRateBps(bitRateBps)
{
}

bool operator==(const MediaMetadata& lhs, const MediaMetadata& rhs) noexcept
{
    return lhs.m_width == rhs.m_width && lhs.m_height == rhs.m_height && lhs.m_duration == rhs.m_duration
        && lhs.m_codec == rhs.m_codec && lhs.m_bitRateBps == rhs.m_bitRateBps;
}
