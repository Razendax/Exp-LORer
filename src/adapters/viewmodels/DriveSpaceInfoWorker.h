#pragma once

#include <filesystem>

#include <QObject>

#include "DriveSpaceInfo.h"
#include "Result.h"

class FileNavigationUseCase;

// Runs FileNavigationUseCase::driveSpaceInfo on whatever thread it has been moved to
// (Architecture.md §5/§14.30) -- the Win32 GetVolumeInformationW/GetDiskFreeSpaceExW calls behind
// it are disk/volume I/O and must never run on the Qt UI thread. Owned and driven by MainWindow,
// which dispatches to and receives from this worker via QMetaObject::invokeMethod(...,
// Qt::QueuedConnection), the same shape FilePreviewViewModel/FilePreviewWorker already establish.
class DriveSpaceInfoWorker : public QObject
{
    Q_OBJECT

public:
    explicit DriveSpaceInfoWorker(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    Result<DriveSpaceInfo> query(const std::filesystem::path& path) const;

private:
    FileNavigationUseCase& m_fileNavigationUseCase;
};
