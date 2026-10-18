#include "DirectoryWatchRegistry.h"

#include <algorithm>

#include "Logging.h"
#include "UnicodeCaseFold.h"

// Windows filesystems are case-insensitive, so watch refcounting keys must be too -- otherwise two
// watch() calls on the same directory differing only in letter case (e.g. "d:\data" vs "D:\Data")
// are treated as independent, un-refcounted watches. UnicodeCaseFold::foldWide (see
// UnicodeCaseFold.h) folds each UTF-16 code unit individually rather than round-tripping through
// UTF-8, since every codepoint it covers (Architecture.md's non-ASCII hard requirement's core
// scripts) fits in one UTF-16 code unit.
DirectoryWatchRegistry::AddOutcome DirectoryWatchRegistry::addWatch(const std::wstring& pathKey, std::function<void()> callback)
{
    const std::wstring normalizedKey = UnicodeCaseFold::foldWide(pathKey);
    const bool isFirstForPath = m_canonicalPathKeys.find(normalizedKey) == m_canonicalPathKeys.end();
    if (isFirstForPath)
    {
        m_canonicalPathKeys[normalizedKey] = pathKey;
    }

    const DirectoryWatchId id = m_nextId++;
    m_entries.push_back(Entry{ id, pathKey, normalizedKey, std::move(callback) });

    if (isFirstForPath)
    {
        Logging::debug("directory watch registry: first watcher for a path (watch " + std::to_string(id) + ")");
    }

    return AddOutcome{ id, isFirstForPath };
}

DirectoryWatchRegistry::RemoveOutcome DirectoryWatchRegistry::removeWatch(DirectoryWatchId id)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry& entry) { return entry.id == id; });
    if (it == m_entries.end())
    {
        return RemoveOutcome{ false, false, {} };
    }

    const std::wstring normalizedKey = it->normalizedKey;
    m_entries.erase(it);

    const bool anyRemainingForPath =
        std::any_of(m_entries.begin(), m_entries.end(), [&normalizedKey](const Entry& entry) { return entry.normalizedKey == normalizedKey; });

    if (anyRemainingForPath)
    {
        return RemoveOutcome{ true, false, {} };
    }

    // Last watcher for this path -- report back the exact casing that was originally used to start
    // the OS-level watch (not necessarily this entry's own casing, if it was a later, differently-
    // cased addWatch() call for the same directory) so the caller's OS-level unwatch call matches.
    const auto canonicalIt = m_canonicalPathKeys.find(normalizedKey);
    const std::wstring canonicalPathKey = canonicalIt != m_canonicalPathKeys.end() ? canonicalIt->second : std::wstring{};
    m_canonicalPathKeys.erase(normalizedKey);

    Logging::debug("directory watch registry: last watcher removed for a path (watch " + std::to_string(id) + ")");
    return RemoveOutcome{ true, true, canonicalPathKey };
}

std::vector<std::function<void()>> DirectoryWatchRegistry::callbacksForPath(const std::wstring& pathKey) const
{
    const std::wstring normalizedKey = UnicodeCaseFold::foldWide(pathKey);
    std::vector<std::function<void()>> callbacks;
    for (const Entry& entry : m_entries)
    {
        if (entry.normalizedKey == normalizedKey)
        {
            callbacks.push_back(entry.callback);
        }
    }
    return callbacks;
}
