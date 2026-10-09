#include "FragmentationViewModel.h"

#include <utility>

#include "FragmentationService.h"
#include "FragmentationUseCase.h"
#include "PathUtf8.h"
#include "TabViewModel.h"

FragmentationViewModel::FragmentationViewModel(FragmentationService& service, NotificationCenter& notificationCenter,
                                                 TabViewModel* tab, QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_notificationCenter(notificationCenter)
    , m_tab(tab)
    , m_currentPath(tab->currentPath())
{
    connect(m_tab, &TabViewModel::currentPathChanged, this, &FragmentationViewModel::onTabCurrentPathChanged);

    // Only this ViewModel's own in-flight scan (if any) cares about a Cancel click -- every other
    // progress card's cancel request is somebody else's concern, same filter-by-id posture
    // DuplicateFinderDialog uses.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this, [this](NotificationCenter::ProgressId id) {
        if (m_scanning && id == m_activeProgressId)
        {
            m_service.cancel(m_activeProgressId);
        }
    });

    refreshForCurrentPath();
}

void FragmentationViewModel::onTabCurrentPathChanged(const std::filesystem::path& path)
{
    m_currentPath = path;
    refreshForCurrentPath();
}

void FragmentationViewModel::setScanning(bool scanning)
{
    if (m_scanning == scanning)
    {
        return;
    }

    m_scanning = scanning;
    emit scanningChanged(m_scanning);
}

void FragmentationViewModel::scanFolder(std::uintmax_t minFileSizeBytes)
{
    startScan(m_currentPath, minFileSizeBytes);
}

void FragmentationViewModel::scanDrive(std::uintmax_t minFileSizeBytes)
{
    startScan(m_currentPath.root_path(), minFileSizeBytes);
}

void FragmentationViewModel::startScan(const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes)
{
    if (m_scanning)
    {
        return;
    }

    setScanning(true);

    m_activeProgressId = m_notificationCenter.createProgressNotification(tr("Scanning for Fragmentation"), /*barCount=*/1,
                                                                            /*cancellable=*/true);

    m_service.requestScan(
        this, m_activeProgressId, scanRoot, minFileSizeBytes,
        [this](std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath) {
            const QString pathText = QString::fromStdString(PathUtf8::toUtf8(currentPath));
            if (totalCandidates == 0)
            {
                // Listing phase -- total not known yet, so an indeterminate bar (max 0) with a
                // running discovered count, rather than a fake "0 / 0" determinate one.
                m_notificationCenter.updateProgressBar(m_activeProgressId, 0, 0, 0, tr("Scanning folder: %1").arg(pathText),
                                                         tr("%1 files found so far").arg(scanned), QString());
            }
            else
            {
                m_notificationCenter.updateProgressBar(m_activeProgressId, 0, static_cast<int>(scanned),
                                                         static_cast<int>(totalCandidates),
                                                         tr("Checking fragmentation: %1").arg(pathText),
                                                         tr("%1 / %2 files scanned").arg(scanned).arg(totalCandidates), QString());
            }
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

            // Correct whether or not the tab is still on the scanned root -- this always shows
            // whatever the tab's *current* path's cached data now is, the newly-written rows
            // included if the tab is still there, or that other folder's own unrelated cached
            // state if not. No generation/stale-result bookkeeping needed, since persistence makes
            // every completed scan's effect valid to show whenever it's next looked at.
            refreshForCurrentPath();
        });
}

void FragmentationViewModel::refreshForCurrentPath()
{
    const std::filesystem::path folder = m_currentPath;

    m_service.requestFilesUnder(this, folder, [this](Result<std::vector<FileFragmentationInfo>> result) {
        if (result.hasError())
        {
            emit scanFailed(QString::fromStdString(result.error().message));
            return;
        }

        const std::vector<FileFragmentationInfo>& files = result.value();
        emit resultsChanged(files, FragmentationUseCase::summarize(files));
    });
}

void FragmentationViewModel::requestOpenContainingFolder(const std::filesystem::path& path)
{
    emit openContainingFolderRequested(path);
}
