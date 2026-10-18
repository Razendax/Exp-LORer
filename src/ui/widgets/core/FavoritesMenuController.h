#pragma once

#include <filesystem>
#include <functional>

#include <QObject>
#include <QString>
#include <QThread>

#include "Favorites.h"
#include "WorkspacePaneId.h"

class DriveSpaceInfoWorker;
class FavoritesViewModel;
class FileNavigationUseCase;
class QAction;
class QMenu;
class TabViewModel;
class WorkspaceController;
class WorkspacePaneWidget;

// The Favorites menu (Architecture.md §14.35), extracted out of MainWindow (Architecture.md
// §14.96) -- nearly self-contained already, following the same owned-sub-object pattern as
// WorkspaceController/FileOperationsController. Owns the QMenu itself (MainWindow just inserts it
// into its own menuBar() via menu()) and the dynamic per-favorite QWidgetAction rows, rebuilt
// lazily on the menu's own aboutToShow.
//
// `paneWidgetResolver` is a callback rather than a direct WorkspaceLayoutWidget& reference: this
// controller is constructed from MainWindow::createMenuBar(), before
// MainWindow::createWorkspace() has constructed its WorkspaceLayoutWidget -- the callback defers
// that lookup to when a favorite item is actually clicked (well after construction), the same
// "resolve lazily" posture FileItemContextMenu's own callbacks already use.
//
// Resolves its own "stat a favorite's target off the UI thread" need via a dedicated
// DriveSpaceInfoWorker + QThread, rather than reaching into MainWindow's own drive-info worker
// (Architecture.md §14.96) -- that incidental coupling is what this extraction specifically
// removes.
class FavoritesMenuController : public QObject
{
    Q_OBJECT

public:
    FavoritesMenuController(FavoritesViewModel& favoritesViewModel, FileNavigationUseCase& fileNavigationUseCase,
                             WorkspaceController& workspaceController,
                             std::function<WorkspacePaneWidget*(WorkspacePaneId)> paneWidgetResolver,
                             QWidget* parent = nullptr);
    ~FavoritesMenuController() override;

    QMenu* menu() const noexcept { return m_menu; }

public slots:
    // Called by MainWindow::onFocusedTabChanged (both its no-focused-tab early return and its
    // normal path) to keep the "Add to Favorites" action's enabled state in sync with the
    // focused tab's current path. Disconnects from the previously-tracked tab first, same
    // bulk-disconnect-then-reconnect shape as MonitoringPanelViewModel::setFocusedTab.
    void setFocusedTab(TabViewModel* tab);

signals:
    // "Manage Favorites..." stays a MainWindow concern (it launches a modal dialog, which needs a
    // QWidget parent this QObject-only controller doesn't have) -- bubbled up the same way
    // MonitoringPanelWidget::monitoringFailed bubbles to MainWindow::onStatusMessage.
    void manageFavoritesRequested();
    void statusMessage(const QString& message);

private:
    void onAddToFavoritesTriggered();
    void onFavoriteItemTriggered(Favorites::Id id);

    // Rebuilds the dynamic per-favorite entries (everything after the static Add to Favorites/
    // Manage Favorites/separator block) -- connected to the menu's own aboutToShow rather than
    // kept live-synced against FavoritesViewModel::favoritesChanged(), so no extra wiring is
    // needed here.
    void rebuildMenuItems();

    void updateActionEnabled();

    FavoritesViewModel& m_favoritesViewModel;
    WorkspaceController& m_workspaceController;
    std::function<WorkspacePaneWidget*(WorkspacePaneId)> m_paneWidgetResolver;

    QThread m_statThread;
    DriveSpaceInfoWorker* m_statWorker = nullptr;

    QMenu* m_menu = nullptr;
    QAction* m_addToFavoritesAction = nullptr;
    TabViewModel* m_trackedTab = nullptr;
};
