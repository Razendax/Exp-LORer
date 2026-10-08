#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "MediaExtensions.h"

// Shared archive-extension classification (Architecture.md §14.28), mirrors MediaExtensions.h.
// Used both by the UI layer (a cheap lexical hint for which context-menu actions to show) and by
// FilePreviewUseCase (widening the folder-preview condition). The disk-verified, authoritative
// check lives in src/adapters/archive/ArchivePathResolver -- this header never touches disk.
namespace ArchiveExtensions
{
    // Shared with MediaExtensions rather than duplicated, so the two classification points can't
    // silently drift apart the same way MediaExtensions.h itself already guards against.
    using MediaExtensions::lowercaseExtension;

    // "photos.tar.gz"/"photos.tar.bz2" are already covered here since path::extension() on them
    // is ".gz"/".bz2" -- no separate compound-suffix check is needed for detection.
    inline bool isArchiveExtension(const std::string& extension)
    {
        static const std::array<std::string, 8> kArchiveExtensions{
            ".zip", ".7z", ".tar", ".gz", ".tgz", ".bz2", ".tbz2", ".rar",
        };
        return std::find(kArchiveExtensions.begin(), kArchiveExtensions.end(), extension) != kArchiveExtensions.end();
    }

    inline bool isArchiveExtension(const std::filesystem::path& path)
    {
        return isArchiveExtension(lowercaseExtension(path));
    }

    // Pure, lexical (no I/O) hint: the outermost ancestor path component (including path itself)
    // with an archive extension, or nullopt if none. This is a UI-layer hint only, NOT
    // authoritative -- a real folder literally named "foo.zip" would also match here; the adapter
    // layer (ArchivePathResolver) re-verifies against real disk state before doing anything, so a
    // mismatch degrades to a disabled action or a graceful Result failure, never a crash.
    inline std::optional<std::filesystem::path> archiveAncestorInPath(const std::filesystem::path& path)
    {
        std::optional<std::filesystem::path> found;
        std::filesystem::path current = path;
        for (;;)
        {
            if (isArchiveExtension(current))
            {
                found = current;
            }

            std::filesystem::path parent = current.parent_path();
            if (parent.empty() || parent == current)
            {
                break;
            }
            current = parent;
        }
        return found;
    }
}
