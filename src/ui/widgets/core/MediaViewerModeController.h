#pragma once

#include <QObject>
#include <QString>

class MediaViewerSearchPanelWidget;
class QAction;
class QActionGroup;
class SidePanelWidget;
class TabViewModel;
class WorkspaceController;
class WorkspaceLayoutWidget;

// Media Viewer mode (Architecture.md §14.61), extracted out of MainWindow (Architecture.md
// §14.96) -- the single biggest coupling hotspot in the class: it bundled the mode toggle action,
// the Media-Viewer slice of onFocusedTabChanged()'s six-tracked-tab-pointer dispatch, and all
// wiring to the Searcher panel. Follows the same owned-sub-object pattern as
// FavoritesMenuController/WorkspaceController, with one deliberate deviation: the mode QAction
// itself is still created (and owned) by MainWindow, since it must exist before createMenuBar()
// runs, which is before this controller can be constructed -- this controller is built once every
// widget it wires to (WorkspaceLayoutWidget, the side panels, the Searcher panel) already exists,
// at the end of MainWindow::createWorkspace().
class MediaViewerModeController : public QObject
{
    Q_OBJECT

public:
    MediaViewerModeController(WorkspaceController& workspaceController, WorkspaceLayoutWidget& workspaceLayoutWidget,
                               SidePanelWidget& leftPanelWidget, SidePanelWidget& rightPanelWidget,
                               MediaViewerSearchPanelWidget& searchPanelWidget, QActionGroup& layoutActionGroup,
                               QAction* modeAction, QObject* parent = nullptr);

    // Resolves which tab Media-Viewer actions (the toggle action, every Searcher-panel control)
    // should act on: the tab actually in Media Viewer mode when one exists, since focus may have
    // moved elsewhere in a split workspace; otherwise the currently-focused tab.
    TabViewModel* targetTab() const;

public slots:
    // Called by MainWindow::onFocusedTabChanged on every focus change, tab or nullptr alike --
    // updates the tracked-tab signal wiring (re-targeting to whichever tab is actually in Media
    // Viewer mode, same as before) and the mode action's enabled state.
    void setFocusedTab(TabViewModel* tab);

signals:
    void statusMessage(const QString& message);

private:
    // Shows/hides the left panel and the right panel's Tags/Preview/Monitoring/Searcher tab
    // buttons, and switches the right panel to the Searcher tab, in response to the tracked tab's
    // own TabMediaViewerViewModel::modeChanged.
    void onModeActiveChanged(bool active);

    WorkspaceController& m_workspaceController;
    WorkspaceLayoutWidget& m_workspaceLayoutWidget;
    SidePanelWidget& m_leftPanelWidget;
    SidePanelWidget& m_rightPanelWidget;
    MediaViewerSearchPanelWidget& m_searchPanelWidget;
    QActionGroup& m_layoutActionGroup;
    QAction* m_modeAction = nullptr;

    // The tab currently wired up for Media-Viewer signal dispatch -- not necessarily the focused
    // tab; see targetTab()'s comment.
    TabViewModel* m_trackedTab = nullptr;

    // The tab actually in Media Viewer mode, independent of which tab/pane currently has focus --
    // set when its TabMediaViewerViewModel::modeChanged(true) fires, cleared on modeChanged(false).
    TabViewModel* m_activeTab = nullptr;

    // Right panel's active tab index right before Media Viewer mode's Searcher tab took over, so
    // exiting the mode restores whichever tab (Tags/Preview/Monitoring) was showing before. -1 if
    // none was active (panel was collapsed).
    int m_rightPanelActiveIndexBeforeMediaViewer = -1;
};
