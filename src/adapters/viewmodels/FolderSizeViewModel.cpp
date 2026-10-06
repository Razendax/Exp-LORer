#include "FolderSizeViewModel.h"

#include <numeric>
#include <utility>

#include "DiskUsageService.h"
#include "DiskUsageUseCase.h"
#include "PathUtf8.h"
#include "TabViewModel.h"

FolderSizeViewModel::FolderSizeViewModel(DiskUsageService& service, NotificationCenter& notificationCenter, TabViewModel* tab,
                                          QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_notificationCenter(notificationCenter)
    , m_tab(tab)
    , m_currentPath(tab->currentPath())
{
    connect(m_tab, &TabViewModel::currentPathChanged, this, &FolderSizeViewModel::onTabCurrentPathChanged);

    // Only this ViewModel's own in-flight scan (if any) cares about a Cancel click -- every other
    // progress card's cancel request is somebody else's concern, same filter-by-id posture
    // FragmentationViewModel uses.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this, [this](NotificationCenter::ProgressId id) {
        if (m_scanning && id == m_activeProgressId)
        {
            m_service.cancel(m_activeProgressId);
        }
    });

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

    m_activeProgressId = m_notificationCenter.createProgressNotification(tr("Scanning for Folder Size"), /*barCount=*/1,
                                                                            /*cancellable=*/true);

    m_service.requestScan(
        this, m_activeProgressId, scanRoot,
        [this](std::size_t scanned, const std::filesystem::path& currentPath) {
            // Always the indeterminate case -- this scanner has no separate "count candidates
            // first" phase (unlike Fragmentation's two-phase scan), so there's never a known total.
            const QString pathText = QString::fromStdString(PathUtf8::toUtf8(currentPath));
            m_notificationCenter.updateProgressBar(m_activeProgressId, 0, 0, 0, tr("Scanning folder: %1").arg(pathText),
                                                     tr("%1 items found so far").arg(scanned), QString());
        },
        [this](Result<void> result) {
            m_notificationCenter.terminateProgressNotification(m_activeProgressId);
            setScanning(false);

            if (result.hasError())
            {
                if (result.error().code != ErrorCode::Cancelled)
                {
                    emit scanFailed(QString::fromStdString(result.error().message));
                }
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
