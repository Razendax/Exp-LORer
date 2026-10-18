#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "FolderChangeEvent.h"
#include "FolderMonitorUseCase.h"

// Recursive folder monitoring for a single tab (Architecture.md §14.24, "Monitoring panel"),
// extracted out of TabViewModel (Architecture.md §14.96). Owned by TabViewModel via `this`-
// parenting, exposed through TabViewModel::monitoring(); callers connect to its signals and call
// its slots directly rather than through TabViewModel forwarders. Multiple tabs may monitor
// concurrently; only one is ever *displayed* at a time by MonitoringPanelViewModel.
class TabMonitoringViewModel : public QObject
{
    Q_OBJECT

public:
    explicit TabMonitoringViewModel(FolderMonitorUseCase& folderMonitorUseCase, QObject* parent = nullptr);
    ~TabMonitoringViewModel() override;

    bool isActive() const noexcept { return m_active; }
    std::filesystem::path root() const { return m_root; }
    bool isPinned() const noexcept { return m_pinned; }
    const std::deque<FolderChangeEvent>& log() const noexcept { return m_log; }
    int total() const noexcept { return m_total; }

public slots:
    // No-op if already active or already starting, or if `currentPath` is VirtualPaths::ThisPC/an
    // archive-virtual path (the Start button is also UI-disabled in that state, but this slot stays
    // defensive). `currentPath` is handed in by the caller (TabViewModel::currentPath() at the time
    // of the call) since navigation state isn't part of this sub-object (Architecture.md §14.96).
    // Clears any previous session's log/total, then dispatches an async watch request for
    // `currentPath` -- opening the directory handle is I/O and does not happen synchronously here
    // (Architecture.md's no-I/O-on-UI-thread rule), so isActive() only flips true once that open
    // actually succeeds; failed is emitted instead on failure.
    void start(const std::filesystem::path& currentPath);

    // No-op if not active and no start is currently in flight. If a start dispatched moments ago
    // hasn't finished opening the directory handle yet, cancels it instead (its completion callback
    // stops the monitor immediately if the open still ends up succeeding). Log/total are
    // deliberately left untouched otherwise (a later start() call clears them) so the last
    // session's results stay visible after stopping.
    void stop();

    // No-op if unchanged, or if not currently active (nothing to pin).
    void setPinned(bool pinned);

signals:
    void activeChanged(bool active);
    void logAppended(const std::vector<FolderChangeEvent>& batch);
    void totalChanged(int total);
    void pinnedChanged(bool pinned);
    void failed(const QString& message);

private:
    FolderMonitorUseCase& m_folderMonitorUseCase;

    // True from start() dispatching StandardFolderMonitor's async open until that open's completion
    // callback runs -- opening the directory handle happens on IFolderMonitor's own worker thread
    // rather than inline, so there is a real gap where a monitor has been requested but m_active is
    // still false. Guards against a redundant second start() dispatch during that gap.
    bool m_starting = false;
    // Set by stop() if it runs during that gap (there is no FolderMonitorId yet to stop via
    // m_folderMonitorUseCase) -- checked by the onStarted completion callback, which stops the
    // monitor immediately instead of activating it if this is set.
    bool m_cancelled = false;
    bool m_active = false;
    std::filesystem::path m_root;
    FolderMonitorId m_id = 0;
    bool m_pinned = false;
    std::deque<FolderChangeEvent> m_log; // capped at kMonitoringLogCap, oldest dropped
    int m_total = 0; // uncapped
};
