#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using DirectoryWatchId = std::uint64_t;

// Pure refcounting bookkeeping for IDirectoryWatcher's watch()/unwatch() semantics -- no Qt, no
// disk I/O, no OS calls. Keyed by a plain path key (StandardDirectoryWatcher passes
// path.wstring()) so multiple independent watch() calls on the same directory only need one
// OS-level watch underneath; this tracks the refcount and tells the caller when to actually
// start/stop the OS-level watch. GTest-covered directly
// (tests/adapters/DirectoryWatchRegistryTest.cpp), mirrors the WorkspaceLayoutTopology/DriveLabel
// precedent for pure logic split out of an otherwise-untestable adapter.
class DirectoryWatchRegistry
{
public:
    struct AddOutcome
    {
        DirectoryWatchId id;
        bool shouldStartOsWatch; // true only when this is the first watcher registered for pathKey
    };

    struct RemoveOutcome
    {
        bool found;
        bool shouldStopOsWatch; // true only when this was the last watcher registered for its path
        std::wstring pathKey;   // valid only when found is true
    };

    AddOutcome addWatch(const std::wstring& pathKey, std::function<void()> callback);

    // No-op (found=false) if id is unknown or already removed.
    RemoveOutcome removeWatch(DirectoryWatchId id);

    std::vector<std::function<void()>> callbacksForPath(const std::wstring& pathKey) const;

private:
    struct Entry
    {
        DirectoryWatchId id;
        std::wstring pathKey;
        std::function<void()> callback;
    };

    std::vector<Entry> m_entries;
    DirectoryWatchId m_nextId = 1;
};
