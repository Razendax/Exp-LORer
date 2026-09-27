#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "IFolderMonitor.h"
#include "Result.h"

// Recursively watches a folder subtree for changes (Architecture.md §14.x). Depends only on
// IFolderMonitor& (same single-Port-dependency shape as DirectoryWatchUseCase), but wraps the
// caller's onChanged callback with a debounce filter before handing it to the Port -- see
// startMonitoring's definition for why.
class FolderMonitorUseCase
{
public:
    explicit FolderMonitorUseCase(IFolderMonitor& folderMonitor);

    // onStarted is forwarded to the Port unchanged (see IFolderMonitor::startMonitoring) -- opening
    // the directory handle is I/O and cannot report success/failure synchronously.
    void startMonitoring(const std::filesystem::path& rootDirectory,
                          std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                          std::function<void(Result<FolderMonitorId>)> onStarted);
    void stopMonitoring(FolderMonitorId id);

private:
    IFolderMonitor& m_folderMonitor;
};
