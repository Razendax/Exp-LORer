#include "DirectoryWatchRegistry.h"

#include <algorithm>

DirectoryWatchRegistry::AddOutcome DirectoryWatchRegistry::addWatch(const std::wstring& pathKey, std::function<void()> callback)
{
    const bool isFirstForPath = std::none_of(m_entries.begin(), m_entries.end(),
                                              [&pathKey](const Entry& entry) { return entry.pathKey == pathKey; });

    const DirectoryWatchId id = m_nextId++;
    m_entries.push_back(Entry{ id, pathKey, std::move(callback) });

    return AddOutcome{ id, isFirstForPath };
}

DirectoryWatchRegistry::RemoveOutcome DirectoryWatchRegistry::removeWatch(DirectoryWatchId id)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry& entry) { return entry.id == id; });
    if (it == m_entries.end())
    {
        return RemoveOutcome{ false, false, {} };
    }

    const std::wstring pathKey = it->pathKey;
    m_entries.erase(it);

    const bool anyRemainingForPath =
        std::any_of(m_entries.begin(), m_entries.end(), [&pathKey](const Entry& entry) { return entry.pathKey == pathKey; });

    return RemoveOutcome{ true, !anyRemainingForPath, pathKey };
}

std::vector<std::function<void()>> DirectoryWatchRegistry::callbacksForPath(const std::wstring& pathKey) const
{
    std::vector<std::function<void()>> callbacks;
    for (const Entry& entry : m_entries)
    {
        if (entry.pathKey == pathKey)
        {
            callbacks.push_back(entry.callback);
        }
    }
    return callbacks;
}
