#pragma once

#include <vector>

#include <QObject>
#include <QString>

#include "FolderChangeEvent.h"

class TabViewModel;

// The single shared ViewModel behind the Monitoring panel (Architecture.md §14.24), retargeted by
// WorkspaceController to whichever pane/tab currently has focus -- same posture as
// TagListViewModel/FilePreviewViewModel, but simpler: all real monitoring logic lives on
// TabViewModel itself, this is a pure retargeting/forwarding shim.
class MonitoringPanelViewModel : public QObject
{
    Q_OBJECT

public:
    explicit MonitoringPanelViewModel(QObject* parent = nullptr);

    TabViewModel* displayedTab() const noexcept { return m_displayedTab; }

public slots:
    // Called by WorkspaceController::retargetFocusedTab(). If the currently displayed tab is
    // pinned, this is a no-op (pin locks the panel regardless of focus). Otherwise switches to
    // `tab` and re-emits its current state.
    void setFocusedTab(TabViewModel* tab);

    // active -> tab->monitoring()->stop(); inactive -> tab->monitoring()->start(tab->currentPath()),
    // on m_displayedTab.
    void toggleStartStop();

    // tab->monitoring()->setPinned(!tab->monitoring()->isPinned()), on m_displayedTab.
    void togglePin();

signals:
    void displayedTabChanged(TabViewModel* tab);

    // Re-emitted straight through from whichever tab is currently displayed.
    void monitoringActiveChanged(bool active);
    void monitoringLogReset(); // fired on displayedTabChanged and on startMonitoring (fresh session)
    void monitoringLogAppended(const std::vector<FolderChangeEvent>& batch);
    void monitoringTotalChanged(int total);
    void monitoringPinnedChanged(bool pinned);
    void monitoringFailed(const QString& message);

private:
    void connectDisplayedTab();
    void disconnectDisplayedTab();
    void emitCurrentState();

    TabViewModel* m_displayedTab = nullptr; // may differ from the focused tab while pinned
};
