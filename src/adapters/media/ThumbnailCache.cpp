#include "ThumbnailCache.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>
#include <vector>

#include <xxhash.h>

#include "PathUtf8.h"

namespace
{
    namespace fs = std::filesystem;

    std::string toHex(std::uint64_t value)
    {
        static const char* kDigits = "0123456789abcdef";
        std::string hex(16, '0');
        for (int i = 15; i >= 0; --i)
        {
            hex[static_cast<std::size_t>(i)] = kDigits[value & 0xF];
            value >>= 4;
        }
        return hex;
    }
}

ThumbnailCache::ThumbnailCache(std::filesystem::path cacheDirectory, std::uintmax_t maxTotalBytes)
    : m_cacheDirectory(std::move(cacheDirectory))
    , m_maxTotalBytes(maxTotalBytes)
{
    std::error_code ec;
    fs::create_directories(m_cacheDirectory, ec);

    scanDiskIndex();
    pruneIfOverCap();
}

std::string ThumbnailCache::cacheFileName(const Key& key) const
{
    const std::string pathUtf8 = PathUtf8::toUtf8(key.path);

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    XXH64_update(state, pathUtf8.data(), pathUtf8.size());
    XXH64_update(state, &key.size, sizeof(key.size));
    XXH64_update(state, &key.mtimeTicks, sizeof(key.mtimeTicks));
    XXH64_update(state, &key.maxWidth, sizeof(key.maxWidth));
    XXH64_update(state, &key.maxHeight, sizeof(key.maxHeight));
    const std::uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    return toHex(hash) + ".bin";
}

std::filesystem::path ThumbnailCache::cacheFilePath(const Key& key) const
{
    return m_cacheDirectory / cacheFileName(key);
}

std::optional<std::vector<std::byte>> ThumbnailCache::get(const Key& key)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string fileName = cacheFileName(key);

    auto memoryIt = m_memoryIndex.find(fileName);
    if (memoryIt != m_memoryIndex.end())
    {
        std::vector<std::byte> bytes = memoryIt->second->bytes;
        m_memoryLru.splice(m_memoryLru.begin(), m_memoryLru, memoryIt->second);
        return bytes;
    }

    const fs::path filePath = m_cacheDirectory / fileName;
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }

    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (size <= 0)
    {
        return std::nullopt;
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    stream.close();

    // Approximate "least recently accessed" eviction (see header comment) by refreshing the
    // file's modification time on every hit.
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(filePath, now, ec);
    if (!ec)
    {
        auto diskIt = m_diskIndex.find(fileName);
        if (diskIt != m_diskIndex.end())
        {
            diskIt->second.lastWrite = now;
        }
    }

    touchMemoryLru(fileName, bytes);

    return bytes;
}

void ThumbnailCache::put(const Key& key, const std::vector<std::byte>& bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string fileName = cacheFileName(key);
    const fs::path filePath = m_cacheDirectory / fileName;

    std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
    if (stream)
    {
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();

        const auto now = fs::file_time_type::clock::now();
        const std::uintmax_t newSize = bytes.size();

        auto diskIt = m_diskIndex.find(fileName);
        if (diskIt != m_diskIndex.end())
        {
            m_currentTotalBytes -= diskIt->second.size;
            diskIt->second = DiskEntry{ newSize, now };
        }
        else
        {
            m_diskIndex.emplace(fileName, DiskEntry{ newSize, now });
        }
        m_currentTotalBytes += newSize;
    }

    touchMemoryLru(fileName, bytes);

    pruneIfOverCap();
}

void ThumbnailCache::touchMemoryLru(const std::string& fileName, const std::vector<std::byte>& bytes)
{
    auto it = m_memoryIndex.find(fileName);
    if (it != m_memoryIndex.end())
    {
        it->second->bytes = bytes;
        m_memoryLru.splice(m_memoryLru.begin(), m_memoryLru, it->second);
        return;
    }

    m_memoryLru.push_front(MemoryEntry{ fileName, bytes });
    m_memoryIndex[fileName] = m_memoryLru.begin();

    while (m_memoryLru.size() > kMemoryLruCapacity)
    {
        const MemoryEntry& evicted = m_memoryLru.back();
        m_memoryIndex.erase(evicted.fileName);
        m_memoryLru.pop_back();
    }
}

void ThumbnailCache::scanDiskIndex()
{
    std::error_code ec;
    if (!fs::exists(m_cacheDirectory, ec))
    {
        return;
    }

    for (const auto& dirEntry : fs::directory_iterator(m_cacheDirectory, fs::directory_options::skip_permission_denied, ec))
    {
        if (!dirEntry.is_regular_file(ec))
        {
            continue;
        }
        const std::uintmax_t size = dirEntry.file_size(ec);
        const fs::file_time_type lastWrite = dirEntry.last_write_time(ec);
        const std::string fileName = PathUtf8::toUtf8(dirEntry.path().filename());
        m_diskIndex[fileName] = DiskEntry{ size, lastWrite };
        m_currentTotalBytes += size;
    }
}

// Evicts least-recently-written entries using the in-memory m_diskIndex built by scanDiskIndex()
// and kept in sync by get()/put() -- no directory scan needed here, so a put() that stays under
// the cap costs nothing beyond the single write it already does.
void ThumbnailCache::pruneIfOverCap()
{
    if (m_currentTotalBytes <= m_maxTotalBytes)
    {
        return;
    }

    std::vector<std::pair<std::string, DiskEntry>> entries(m_diskIndex.begin(), m_diskIndex.end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second.lastWrite < b.second.lastWrite; });

    for (const auto& [fileName, entry] : entries)
    {
        if (m_currentTotalBytes <= m_maxTotalBytes)
        {
            break;
        }
        std::error_code removeEc;
        fs::remove(m_cacheDirectory / fileName, removeEc);
        if (!removeEc)
        {
            m_currentTotalBytes -= entry.size;
            m_diskIndex.erase(fileName);
        }
    }
}
