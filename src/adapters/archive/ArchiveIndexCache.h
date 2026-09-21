#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>

#include "LibArchiveReader.h"
#include "Result.h"

// Small in-memory-only LRU (Architecture.md §14.28) caching LibArchiveReader::listEntries's flat
// entry list, keyed by (path, size, mtime), so navigating between folders inside the *same*
// archive doesn't re-scan the whole compressed stream on every listDirectory/stat call. No disk
// persistence, no size-based eviction policy -- capacity is a small fixed entry count. A changed
// source file naturally produces a different key (same staleness-free posture as ThumbnailCache).
//
// The owning StandardFileSystemRepository is called from both the UI thread and
// FilePreviewViewModel's background preview QThread, so this cache is genuinely shared mutable
// state across threads -- entriesFor() guards every access with m_mutex.
class ArchiveIndexCache
{
public:
    Result<std::vector<LibArchiveReader::Entry>> entriesFor(const std::filesystem::path& archiveFile);

private:
    struct CacheKey
    {
        std::filesystem::path path;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type mtime{};

        bool operator==(const CacheKey&) const = default;
    };

    struct CacheEntry
    {
        CacheKey key;
        std::vector<LibArchiveReader::Entry> entries;
    };

    static constexpr std::size_t kCapacity = 4;
    std::mutex m_mutex;
    std::deque<CacheEntry> m_entries;
};
