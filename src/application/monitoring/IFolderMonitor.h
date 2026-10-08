#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "FolderChangeEvent.h"
#include "Result.h"

using FolderMonitorId = std::uint64_t;

// Port for recursively watching a directory subtree for create/delete/modify changes
// (Architecture.md §14.x, "Monitoring panel"), kept separate from IDirectoryWatcher (Interface
// Segregation -- IDirectoryWatcher is intentionally non-recursive and reports no path/kind, see
// Architecture.md §14.x). Implemented by src/adapters/filesystem/StandardFolderMonitor.
class IFolderMonitor
{
public:
    virtual ~IFolderMonitor() = default;

    // Recursively watches rootDirectory (including every subdirectory) for create/delete/modify.
    // onChanged is invoked with a batch of events already marshaled onto the calling (UI) thread --
    // implementations own their own background I/O, callers never see raw OS notification threading.
    //
    // Unlike most other Ports, this cannot report success/failure via a return value: opening the
    // directory handle is disk/volume I/O (can block for a long time against an unresponsive
    // network share) and must never run on the calling (UI) thread. onStarted is invoked exactly
    // once instead, already marshaled onto the calling thread the same way onChanged's batches are
    // -- with a failure for a non-real path (This PC sentinel, an archive-virtual path, a path that
    // doesn't exist), same "Result failure, not a crash" posture as IDirectoryWatcher::watch used to
    // report synchronously.
    virtual void startMonitoring(const std::filesystem::path& rootDirectory,
                                  std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                                  std::function<void(Result<FolderMonitorId>)> onStarted) = 0;

    // No-op if id is unknown/already stopped.
    virtual void stopMonitoring(FolderMonitorId id) = 0;
};
