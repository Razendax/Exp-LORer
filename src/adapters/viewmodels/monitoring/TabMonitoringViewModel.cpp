#include "TabMonitoringViewModel.h"

#include <QPointer>

#include "ArchiveExtensions.h"
#include "VirtualPaths.h"

namespace
{
    constexpr std::size_t kMonitoringLogCap = 200;
}

TabMonitoringViewModel::TabMonitoringViewModel(FolderMonitorUseCase& folderMonitorUseCase, QObject* parent)
    : QObject(parent)
    , m_folderMonitorUseCase(folderMonitorUseCase)
{
}

TabMonitoringViewModel::~TabMonitoringViewModel()
{
    // A still-active monitor is normally stopped by WorkspacePaneViewModel::closeTab() before a tab
    // (and this sub-object with it) is destroyed, but guard here too for any other destruction
    // path. A monitor whose start() open is still in flight (StandardFolderMonitor now opens the
    // directory handle on its own worker thread, not inline) needs no equivalent handling here: its
    // completion callback captures a QPointer guard on `this`, not `this` itself, and stops the
    // now-ownerless monitor by that captured pointer directly if the guard is already null by the
    // time the open finishes -- see start()'s comment.
    if (m_active)
    {
        m_folderMonitorUseCase.stopMonitoring(m_id);
    }
}

void TabMonitoringViewModel::start(const std::filesystem::path& currentPath)
{
    if (m_active || m_starting)
    {
        return;
    }

    const std::filesystem::path root = currentPath;
    if (root.empty() || root == VirtualPaths::ThisPC || ArchiveExtensions::archiveAncestorInPath(root))
    {
        return;
    }

    m_log.clear();
    m_total = 0;
    m_root = root;
    m_starting = true;
    m_cancelled = false;

    // Opening the directory handle is disk I/O and now happens on IFolderMonitor's own worker
    // thread rather than synchronously here (Architecture.md's no-I/O-on-UI-thread rule) -- onStarted
    // reports the outcome once it's known. `guard` detects this sub-object being destroyed while
    // the open is still in flight; `folderMonitorUseCase` is captured separately (a plain pointer to
    // a CompositionRoot-owned, long-lived object) so the now-ownerless monitor can still be stopped
    // via it even if `this`/`guard` is already gone by the time the callback runs.
    QPointer<TabMonitoringViewModel> guard(this);
    FolderMonitorUseCase* const folderMonitorUseCase = &m_folderMonitorUseCase;

    m_folderMonitorUseCase.startMonitoring(
        root,
        [this](const std::vector<FolderChangeEvent>& batch) {
            for (const FolderChangeEvent& event : batch)
            {
                if (m_log.size() >= kMonitoringLogCap)
                {
                    m_log.pop_front();
                }
                m_log.push_back(event);
            }
            m_total += static_cast<int>(batch.size());

            emit logAppended(batch);
            emit totalChanged(m_total);
        },
        [this, guard, folderMonitorUseCase](Result<FolderMonitorId> result) {
            if (!guard)
            {
                // This sub-object was destroyed while the open was still in flight -- stop the
                // now-ownerless monitor immediately (if it actually succeeded) rather than leaking
                // it for the rest of the app's lifetime.
                if (result)
                {
                    folderMonitorUseCase->stopMonitoring(result.value());
                }
                return;
            }

            m_starting = false;

            if (m_cancelled)
            {
                // stop() ran while this was still in flight (there was no FolderMonitorId yet to
                // stop it by) -- honor that now instead of activating it.
                m_cancelled = false;
                if (result)
                {
                    folderMonitorUseCase->stopMonitoring(result.value());
                }
                return;
            }

            if (!result)
            {
                emit failed(QString::fromStdString(result.error().message));
                return;
            }

            m_id = result.value();
            m_active = true;
            emit activeChanged(true);
        });
}

void TabMonitoringViewModel::stop()
{
    if (m_starting)
    {
        // The open dispatched moments ago hasn't finished yet (Architecture.md's no-I/O-on-UI-thread
        // rule means that can take a while against an unresponsive network share) -- there is no
        // FolderMonitorId yet to stop it by. Flag it so start()'s completion callback stops it
        // immediately once it does finish, instead of activating it.
        m_cancelled = true;
        return;
    }

    if (!m_active)
    {
        return;
    }

    m_folderMonitorUseCase.stopMonitoring(m_id);
    m_active = false;
    m_pinned = false;

    emit activeChanged(false);
    emit pinnedChanged(false);
}

void TabMonitoringViewModel::setPinned(bool pinned)
{
    if (pinned == m_pinned || !m_active)
    {
        return;
    }

    m_pinned = pinned;
    emit pinnedChanged(pinned);
}
