#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QObject>

#include "IFolderMonitor.h"
#include "Result.h"

// Recursively watches a directory subtree for create/delete/modify changes via
// ReadDirectoryChangesW (Architecture.md §14.x, "Monitoring panel"), one background std::thread
// per active monitor (matches WindowsConPtyProcess's "one thread per resource" shape -- concurrent
// monitors are expected to be rare). Windows-only in v1, same posture as every other Windows-first
// Port (ShellContextMenuProvider, WindowsConPtyProcess): startMonitoring fails with
// ErrorCode::IoError on any other platform. Falls under the existing OS-shell/live-notification
// carve-out (Architecture.md §11) -- no automated adapter test, smoke-tested manually, same posture
// as StandardDirectoryWatcher/WindowsConPtyProcess.
class StandardFolderMonitor : public QObject, public IFolderMonitor
{
    Q_OBJECT

public:
    explicit StandardFolderMonitor(QObject* parent = nullptr);
    ~StandardFolderMonitor() override;

    Result<FolderMonitorId> startMonitoring(const std::filesystem::path& rootDirectory,
                                             std::function<void(const std::vector<FolderChangeEvent>&)> onChanged) override;
    void stopMonitoring(FolderMonitorId id) override;

private:
    struct MonitorState;

    // Closes the directory handle (unblocking the pending ReadDirectoryChangesW with
    // ERROR_OPERATION_ABORTED) and joins the worker thread. Idempotent; used by both
    // stopMonitoring() and the destructor.
    void stopMonitor(MonitorState& state);

    std::unordered_map<FolderMonitorId, std::unique_ptr<MonitorState>> m_monitors;
    FolderMonitorId m_nextId = 1;
};
