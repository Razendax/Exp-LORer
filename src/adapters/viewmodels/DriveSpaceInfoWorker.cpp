#include "DriveSpaceInfoWorker.h"

#include "FileNavigationUseCase.h"

DriveSpaceInfoWorker::DriveSpaceInfoWorker(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
{
}

Result<DriveSpaceInfo> DriveSpaceInfoWorker::query(const std::filesystem::path& path) const
{
    return m_fileNavigationUseCase.driveSpaceInfo(path);
}
