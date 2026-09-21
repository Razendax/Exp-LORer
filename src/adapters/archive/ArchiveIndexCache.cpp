#include "ArchiveIndexCache.h"

#include <algorithm>
#include <system_error>

Result<std::vector<LibArchiveReader::Entry>> ArchiveIndexCache::entriesFor(const std::filesystem::path& archiveFile)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto size = fs::file_size(archiveFile, ec);
    const auto mtime = fs::last_write_time(archiveFile, ec);
    if (ec)
    {
        return Result<std::vector<LibArchiveReader::Entry>>::failure(Error(ErrorCode::IoError, ec.message()));
    }

    const CacheKey key{ archiveFile, size, mtime };

    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&key](const CacheEntry& cached) { return cached.key == key; });
    if (it != m_entries.end())
    {
        CacheEntry hit = std::move(*it);
        m_entries.erase(it);
        m_entries.push_back(std::move(hit));
        return Result<std::vector<LibArchiveReader::Entry>>::success(m_entries.back().entries);
    }

    // listEntries() re-reads the archive from disk -- deliberately done while still holding
    // m_mutex (rather than only locking around the m_entries mutation below) so two threads
    // racing on the same not-yet-cached archive can't both pay the full read cost.
    auto listed = LibArchiveReader::listEntries(archiveFile);
    if (!listed)
    {
        return Result<std::vector<LibArchiveReader::Entry>>::failure(std::move(listed).error());
    }

    m_entries.push_back(CacheEntry{ key, listed.value() });
    if (m_entries.size() > kCapacity)
    {
        m_entries.pop_front();
    }

    return Result<std::vector<LibArchiveReader::Entry>>::success(std::move(listed).value());
}
