#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "Result.h"

// Thin wrapper over libarchive's archive_read_* C API (Architecture.md §14.28). Only the in-scope
// formats/filters are enabled (zip/7z/tar/gzip/bzip2/rar) rather than
// archive_read_support_format_all(), so an unrelated format (ISO/CAB/...) is never silently
// exposed as browsable.
namespace LibArchiveReader
{
    struct Entry
    {
        // '/'-separated, relative to the archive root; never has a trailing separator.
        std::filesystem::path relativePath;
        std::uintmax_t size = 0;
        std::chrono::system_clock::time_point modificationTime;
        bool isDirectory = false;
    };

    // One read pass over the whole archive; flat (not just one directory level) -- callers
    // (ArchiveIndexCache) group by prefix themselves.
    Result<std::vector<Entry>> listEntries(const std::filesystem::path& archiveFile);

    // Extracts every entry at or under entryPrefix into destinationDirectory, one read pass:
    //  - entryPrefix empty: the whole archive, preserving its full relative directory structure.
    //  - entryPrefix names a file: that one file, written as destinationDirectory/<its filename>.
    //  - entryPrefix names a folder: its contents, written under destinationDirectory with paths
    //    relative to entryPrefix (so destinationDirectory becomes the extracted copy of that folder).
    Result<void> extractEntries(const std::filesystem::path& archiveFile, const std::filesystem::path& entryPrefix,
                                 const std::filesystem::path& destinationDirectory);
}
