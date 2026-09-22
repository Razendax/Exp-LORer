#pragma once

#include <filesystem>
#include <functional>

#include <QObject>

#include "DirectoryWatchRegistry.h"
#include "IDirectoryWatcher.h"
#include "Result.h"

class QFileSystemWatcher;

// QFileSystemWatcher-backed IDirectoryWatcher (Architecture.md §14.x). Non-recursive by
// construction -- Qt's backend reports only immediate-child changes of a watched directory, which
// is exactly what the Port contract requires. Bookkeeping (refcounting multiple independent
// watch() calls onto one OS-level watch) is delegated to DirectoryWatchRegistry, kept Qt-free and
// separately GTest-covered; this class is thin OS/Qt glue on top of it, smoke-tested manually only
// (same OS-shell carve-out posture as ShellContextMenuProvider, Architecture.md §11). Delivers
// change notifications synchronously on whatever thread they arrive on (the Qt event-loop thread
// this instance lives on) -- no manual thread hop needed (Architecture.md §14.x "Threading").
class StandardDirectoryWatcher : public QObject, public IDirectoryWatcher
{
    Q_OBJECT

public:
    explicit StandardDirectoryWatcher(QObject* parent = nullptr);
    ~StandardDirectoryWatcher() override;

    Result<DirectoryWatchId> watch(const std::filesystem::path& directory, std::function<void()> onChanged) override;
    void unwatch(DirectoryWatchId id) override;

private:
    void onDirectoryChanged(const QString& path);

    QFileSystemWatcher* m_watcher = nullptr;
    DirectoryWatchRegistry m_registry;
};
