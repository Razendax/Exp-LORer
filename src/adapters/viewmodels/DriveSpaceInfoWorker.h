#pragma once

#include <filesystem>

#include <QObject>

#include "DriveSpaceInfo.h"
#include "FileNode.h"
#include "Result.h"

class FileNavigationUseCase;

// Runs FileNavigationUseCase calls on whatever thread it has been moved to (Architecture.md
// §5/§14.30) -- the Win32 calls behind them are disk/volume I/O and must never run on the Qt UI
// thread. Owned and driven by MainWindow, which dispatches to and receives from this worker via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection), the same shape FilePreviewViewModel/
// FilePreviewWorker already establish.
class DriveSpaceInfoWorker : public QObject
{
    Q_OBJECT

public:
    explicit DriveSpaceInfoWorker(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    Result<DriveSpaceInfo> query(const std::filesystem::path& path) const;

    // Used by MainWindow::onFavoriteItemTriggered (Architecture.md §14.35): stat()ing a favorite's
    // target is disk I/O too, and this worker's thread is already set up for exactly this kind of
    // one-off off-UI-thread FileNavigationUseCase call -- no need for a second dedicated thread.
    Result<FileNode> stat(const std::filesystem::path& path) const;

private:
    FileNavigationUseCase& m_fileNavigationUseCase;
};
