#pragma once

#include <QString>
#include <QWidget>

class MonitoringPanelViewModel;
class TabViewModel;
class MonitoringLogModel;
class QPushButton;
class QToolButton;
class QListView;
class QLabel;

// The Monitoring right-panel tab (Architecture.md §14.24): a Start/Stop button plus a pin button
// (enabled only while monitoring is active), an autoscrolling log of the displayed tab's recursive
// folder-monitoring events, and a running Total count. Rebinds whenever
// MonitoringPanelViewModel::displayedTabChanged fires (pane focus changes, or the pinned tab is
// unpinned/closed) -- same "rebuild-on-ViewModel-signal" shape as TagPanelWidget.
class MonitoringPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MonitoringPanelWidget(MonitoringPanelViewModel* viewModel, QWidget* parent = nullptr);

signals:
    // Bubbled up so MainWindow can report it via its transient status bar, same posture as
    // WorkspacePaneWidget::navigationFailed.
    void monitoringFailed(const QString& message);

private:
    void onDisplayedTabChanged(TabViewModel* tab);
    void updateStartStopEnabled();
    void onMonitoringActiveChanged(bool active);
    void onMonitoringPinnedChanged(bool pinned);
    void onMonitoringTotalChanged(int total);
    void onMonitoringLogReset();

    MonitoringPanelViewModel* m_viewModel = nullptr;
    MonitoringLogModel* m_logModel = nullptr;
    TabViewModel* m_trackedTab = nullptr; // whichever tab's currentPathChanged we're bound to

    QPushButton* m_startStopButton = nullptr;
    QToolButton* m_pinButton = nullptr;
    QListView* m_logView = nullptr;
    QLabel* m_totalLabel = nullptr;
};
