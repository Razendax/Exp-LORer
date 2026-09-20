#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// On-disk cache for encoded image/video-poster thumbnail bytes (Architecture.md §8, §14.25).
// Entries are keyed by path+size+mtime+requested dimensions -- a changed source file naturally
// produces a different cache filename, so no separate staleness bookkeeping is needed. A small
// in-memory LRU sits in front of the on-disk store. Written so the future grid-icon thumbnail
// feature can reuse it unchanged.
class ThumbnailCache
{
public:
    struct Key
    {
        std::filesystem::path path;
        std::uintmax_t size = 0;
        std::int64_t mtimeTicks = 0;
        int maxWidth = 0;
        int maxHeight = 0;
    };

    // cacheDirectory is created (recursively) if missing. maxTotalBytes caps total on-disk size
    // (default 512MB, Architecture.md §8); least-recently-accessed entries are pruned on
    // construction and after any write that pushes the total over the cap. "Least recently
    // accessed" is approximated by file modification time (touched on every cache hit), since
    // NTFS last-access-time tracking is commonly disabled for performance.
    explicit ThumbnailCache(std::filesystem::path cacheDirectory, std::uintmax_t maxTotalBytes = 512ull * 1024 * 1024);

    std::optional<std::vector<std::byte>> get(const Key& key);
    void put(const Key& key, const std::vector<std::byte>& bytes);

private:
    std::string cacheFileName(const Key& key) const;
    std::filesystem::path cacheFilePath(const Key& key) const;
    void scanDiskIndex();
    void pruneIfOverCap();
    void touchMemoryLru(const std::string& fileName, const std::vector<std::byte>& bytes);

    std::filesystem::path m_cacheDirectory;
    std::uintmax_t m_maxTotalBytes;

    struct MemoryEntry
    {
        std::string fileName;
        std::vector<std::byte> bytes;
    };

    std::mutex m_mutex;
    std::list<MemoryEntry> m_memoryLru; // front = most recently used
    std::unordered_map<std::string, std::list<MemoryEntry>::iterator> m_memoryIndex;
    static constexpr std::size_t kMemoryLruCapacity = 20;

    // In-memory mirror of what's on disk (size + last-write per cache file), built once from a
    // directory scan in the constructor and kept in sync incrementally by get()/put() from then
    // on. Lets pruneIfOverCap() decide whether/what to evict without re-scanning the whole cache
    // directory on every put().
    struct DiskEntry
    {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type lastWrite{};
    };
    std::unordered_map<std::string, DiskEntry> m_diskIndex;
    std::uintmax_t m_currentTotalBytes = 0;
};
