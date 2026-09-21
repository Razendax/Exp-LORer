#pragma once

#include <filesystem>
#include <optional>

// Given any path, walks its ancestor components looking for the outermost one that is both (a)
// archive-extensioned and (b) a real fs::is_regular_file on disk -- the disk-verified,
// authoritative counterpart to ArchiveExtensions::archiveAncestorInPath's lexical UI-layer hint
// (Architecture.md §14.28). Nested archives (an archive entry that is itself an archive) are not
// resolved further -- only a real on-disk file can ever match, so an inner ".zip" stored as a mere
// entry inside the outer archive is just an ordinary, non-navigable file entry.
namespace ArchivePathResolver
{
    struct Resolution
    {
        std::filesystem::path archiveFile;
        // Empty when path names the archive file's own root; otherwise the entry's path relative
        // to archiveFile.
        std::filesystem::path entryPathInArchive;
    };

    std::optional<Resolution> resolve(const std::filesystem::path& path);
}
