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

    void startMonitoring(const std::filesystem::path& rootDirectory,
                          std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                          std::function<void(Result<FolderMonitorId>)> onStarted) override;
    void stopMonitoring(FolderMonitorId id) override;

private:
    struct MonitorState;

    // Blocking cleanup used only by the destructor (a brief stall during app shutdown is fine,
    // unlike during interactive use): signals the worker to stop, joins it, then closes the
    // directory handle/stop event. Safe to call even if the worker is still mid-open (its own
    // stopRequested check then skips straight to returning, leaving directoryHandle/stopEvent for
    // this function to close once join() unblocks).
    void stopMonitorBlocking(MonitorState& state);

    // Removes a monitor that failed to ever produce a usable handle (CreateFileW/CreateEventW
    // failed) -- called back via QMetaObject::invokeMethod from the worker thread that discovered
    // the failure, right before that thread exits. join() here is a formality (the thread has
    // already posted this call and is on its way out) but keeps stopMonitorBlocking-style
    // "always join before touching thread state" symmetry.
    void discardMonitor(FolderMonitorId id);

    std::unordered_map<FolderMonitorId, std::unique_ptr<MonitorState>> m_monitors;
    FolderMonitorId m_nextId = 1;
};
