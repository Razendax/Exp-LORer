#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

#include "PathUtf8.h"

// Shared image/video extension classification used by both FilePreviewUseCase (routing text vs.
// image vs. video previews) and the adapters/media MediaDecoder (routing to VipsImageDecoder vs.
// FFmpegMediaDecoder) -- kept in one place so the two dispatch points can't silently drift apart
// when an extension is added to one but not the other.
namespace MediaExtensions
{
    inline std::string lowercaseExtension(const std::filesystem::path& path)
    {
        std::string extension = PathUtf8::toUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    inline bool isImageExtension(const std::string& extension)
    {
        static const std::array<std::string, 6> kImageExtensions{ ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp" };
        return std::find(kImageExtensions.begin(), kImageExtensions.end(), extension) != kImageExtensions.end();
    }

    inline bool isVideoExtension(const std::string& extension)
    {
        static const std::array<std::string, 6> kVideoExtensions{ ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".webm" };
        return std::find(kVideoExtensions.begin(), kVideoExtensions.end(), extension) != kVideoExtensions.end();
    }
}
