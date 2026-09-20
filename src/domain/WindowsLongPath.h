#pragma once
#include <filesystem>
#include <string>

// Prefixes a path with the `\\?\` extended-length marker so Windows APIs and std::filesystem
// bypass the legacy MAX_PATH (260-character) limit. A no-op on paths already under that limit,
// already prefixed, or on non-Windows platforms. Shared by every adapter that opens files
// directly by path (StandardFileSystemRepository, VipsImageDecoder, FFmpegMediaDecoder,
// CachingMediaDecoder) so the long-path handling can't silently drift between copies.
namespace WindowsLongPath
{
    inline std::filesystem::path withPrefix(const std::filesystem::path& path)
    {
#ifdef _WIN32
        const std::wstring native = path.wstring();
        constexpr std::size_t maxPath = 260;
        if (native.size() < maxPath || native.rfind(LR"(\\?\)", 0) == 0)
        {
            return path;
        }
        return std::filesystem::path(LR"(\\?\)" + native);
#else
        return path;
#endif
    }
}
