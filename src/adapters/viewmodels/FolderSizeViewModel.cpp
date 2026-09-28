#include "FolderSizeViewModel.h"

#include <numeric>
#include <utility>

#include "DiskUsageService.h"
#include "DiskUsageUseCase.h"
#include "TabViewModel.h"

FolderSizeViewModel::FolderSizeViewModel(DiskUsageService& service, TabViewModel* tab, QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_tab(tab)
    , m_currentPath(tab->currentPath())
{
    connect(m_tab, &TabViewModel::currentPathChanged, this, &FolderSizeViewModel::onTabCurrentPathChanged);

    refreshForCurrentPath();
}

void FolderSizeViewModel::onTabCurrentPathChanged(const std::filesystem::path& path)
{
    m_currentPath = path;
    refreshForCurrentPath();
}

void FolderSizeViewModel::setScanning(bool scanning)
{
    if (m_scanning == scanning)
    {
        return;
    }

    m_scanning = scanning;
    emit scanningChanged(m_scanning);
}

void FolderSizeViewModel::scanFolder()
{
    startScan(m_currentPath);
}

void FolderSizeViewModel::scanDrive()
{
    startScan(m_currentPath.root_path());
}

void FolderSizeViewModel::startScan(const std::filesystem::path& scanRoot)
{
    if (m_scanning)
    {
        return;
    }

    setScanning(true);

    m_service.requestScan(this, scanRoot, [this](Result<void> result) {
        setScanning(false);

        if (result.hasError())
        {
            emit scanFailed(QString::fromStdString(result.error().message));
            return;
        }

        refreshForCurrentPath();
    });
}

void FolderSizeViewModel::refreshForCurrentPath()
{
    const std::filesystem::path folder = m_currentPath;

    m_service.requestChildren(this, folder, [this, folder](Result<std::vector<DiskUsageNode>> result) {
        if (result.hasError())
        {
            emit scanFailed(QString::fromStdString(result.error().message));
            return;
        }

        const std::vector<DiskUsageNode>& children = result.value();
        const std::uintmax_t totalSize =
            std::accumulate(children.begin(), children.end(), std::uintmax_t{ 0 },
                             [](std::uintmax_t sum, const DiskUsageNode& node) { return sum + node.size; });

        std::vector<DiskUsageNode> top = DiskUsageUseCase::topChildren(children, 10);

        emit dataChanged(folder, top, totalSize);
    });
}

void FolderSizeViewModel::onSegmentClicked(const std::filesystem::path& path)
{
    emit navigationRequested(path);
}
