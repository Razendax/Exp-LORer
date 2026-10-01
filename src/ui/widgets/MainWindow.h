#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include <QList>
#include <QMainWindow>
#include <QThread>

#include "AppConfig.h"
#include "Favorites.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "FileNode.h"
#include "SplitLayout.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QResizeEvent;
class QMenu;
class QLineEdit;
class QToolBar;
class AppConfigStore;
class HighlightThemeViewModel;
class FileDecorationsViewModel;
class FavoritesViewModel;
class NotificationCenter;
class NotificationSettingsViewModel;
class ColumnVisibilitySettingsViewModel;
class SyntaxHighlightEngine;
class TagManagementUseCase;
class TagChangeNotifier;
class WorkspaceController;
class WorkspaceLayoutWidget;
class TagPanelWidget;
class SidePanelWidget;
class FolderTreeView;
class PreviewPanelWidget;
class MonitoringPanelWidget;
class NotificationOverlayWidget;
class TabViewModel;
class MainStatusBarWidget;
class DriveSpaceInfoWorker;
class DiskUsageService;

// Application shell: menu bar (File/Edit/View/Favorites/Tools/Help), a small Layout toolbar, and
// a WorkspaceLayoutWidget as central widget hosting up to 4 independent WorkspacePaneWidget
// mini-browsers per the active SplitLayout (Architecture.md §14). Each pane owns its own
// tabs/navigation toolbar/view mode — MainWindow itself no longer holds any of that state.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(WorkspaceController* workspaceController, AppConfigStore& configStore, TagManagementUseCase& tagManagementUseCase,
               TagChangeNotifier& tagChangeNotifier, SyntaxHighlightEngine& syntaxHighlightEngine,
               HighlightThemeViewModel& highlightThemeViewModel, FileDecorationsViewModel& fileDecorationsViewModel,
               FavoritesViewModel& favoritesViewModel, NotificationCenter& notificationCenter,
               NotificationSettingsViewModel& notificationSettingsViewModel,
               ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, DiskUsageService& diskUsageService,
               const AppConfig& initialConfig, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    // Captures window geometry + WorkspaceController::captureConfig() and saves it via
    // m_configStore before chaining to QMainWindow::closeEvent (Architecture.md §14.14 — the only
    // place a save happens; no periodic/live autosave).
    void closeEvent(QCloseEvent* event) override;

    // Watches m_searchBar for Escape (clears the box and exits search on the focused tab).
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Repositions m_notificationOverlayWidget to the bottom-right corner on every resize.
    void resizeEvent(QResizeEvent* event) override;

private:
    void createLayoutActions();
    void createSortByActions();
    void createContextMenuModeAction();
    void createShowHiddenFilesAction();
    void createMenuBar();
    void createFavoritesMenu();
    void createLayoutToolBar();
    void createSearchBar(QToolBar* toolBar);
    void createWorkspace();
    void createFolderTree();
    void createMainStatusBar();
    void showTagManagerDialog();
    void showSettingsDialog();
    void showManageFavoritesDialog();

    void onLayoutChanged(SplitLayout layout);
    void onStatusMessage(const QString& message);
    void onFocusedTabChanged(TabViewModel* tab);
    void onSortOrderChanged(SortCriterion criterion, bool ascending);
    void onActiveTabPathChanged(const std::filesystem::path& path);
    void onDriveSpaceInfoReady(int generation, Result<DriveSpaceInfo> result);
    void onSearchBarReturnPressed();
    void onSearchBarTextEdited(const QString& text);

    // Navigates the focused pane's active tab to `path` (through WorkspacePaneWidget's
    // monitored-scope guard) -- the left panel's folder tree click/Enter/"Open" (Architecture.md
    // §14.41).
    void navigateFocusedPaneTo(const std::filesystem::path& path);

    // Favorites menu (Architecture.md §14.35).
    void onAddToFavoritesTriggered();
    void onFavoriteItemTriggered(Favorites::Id id);
    void rebuildFavoritesMenuItems();
    void updateFavoritesActionEnabled();
    void onFavoritesTrackedSelectionChanged(const std::vector<FileNode>& entries);
    void onFavoritesTrackedPathChanged(const std::filesystem::path& path);

    WorkspaceController* m_workspaceController = nullptr;
    AppConfigStore& m_configStore;
    TagManagementUseCase& m_tagManagementUseCase;
    TagChangeNotifier& m_tagChangeNotifier;
    SyntaxHighlightEngine& m_syntaxHighlightEngine;
    HighlightThemeViewModel& m_highlightThemeViewModel;
    FileDecorationsViewModel& m_fileDecorationsViewModel;
    FavoritesViewModel& m_favoritesViewModel;
    NotificationCenter& m_notificationCenter;
    NotificationSettingsViewModel& m_notificationSettingsViewModel;
    ColumnVisibilitySettingsViewModel& m_columnVisibilitySettingsViewModel;
    DiskUsageService& m_diskUsageService;
    WorkspaceLayoutWidget* m_workspaceLayoutWidget = nullptr;
    TagPanelWidget* m_tagPanelWidget = nullptr;
    PreviewPanelWidget* m_previewPanelWidget = nullptr;
    MonitoringPanelWidget* m_monitoringPanelWidget = nullptr;
    NotificationOverlayWidget* m_notificationOverlayWidget = nullptr;
    SidePanelWidget* m_rightPanelWidget = nullptr;
    SidePanelWidget* m_leftPanelWidget = nullptr;
    FolderTreeView* m_folderTreeView = nullptr;
    TabViewModel* m_sortTrackedTab = nullptr;
    TabViewModel* m_searchTrackedTab = nullptr;
    TabViewModel* m_driveInfoTrackedTab = nullptr;
    QLineEdit* m_searchBar = nullptr;
    QAction* m_advancedSearchAction = nullptr;
    MainStatusBarWidget* m_mainStatusBarWidget = nullptr;

    // Favorites menu (Architecture.md §14.35): rebuilt lazily on QMenu::aboutToShow rather than
    // kept live-synced, so no direct favoritesChanged() wiring is needed here.
    QMenu* m_favoritesMenu = nullptr;
    QAction* m_addToFavoritesAction = nullptr;
    TabViewModel* m_favoritesTrackedTab = nullptr;

    // Drive/volume info query (Architecture.md §14.30) runs on m_driveInfoThread rather than the
    // Qt UI thread -- GetVolumeInformationW/GetDiskFreeSpaceExW behind it are disk/volume I/O.
    // m_driveInfoGeneration invalidates a stale in-flight query the same way
    // FilePreviewViewModel::m_generation does: bumped on every onActiveTabPathChanged call, so a
    // result that arrives after a newer navigation/focus change is simply discarded.
    QThread m_driveInfoThread;
    DriveSpaceInfoWorker* m_driveInfoWorker = nullptr;
    int m_driveInfoGeneration = 0;

    // One QAction per SplitLayout (same order as the anonymous-namespace kLayouts array in the
    // .cpp), shared verbatim between the "Layout" toolbar and the View > Layout submenu so both
    // stay in sync automatically.
    QActionGroup* m_layoutActionGroup = nullptr;
    QList<QAction*> m_layoutActions;

    // "Sort by" submenu under View, retargeted to the focused pane's active tab
    // (WorkspaceController::focusedTabChanged) rather than owned per-pane. m_sortCriterionActions
    // order mirrors the anonymous-namespace kSortCriteria array in the .cpp.
    QMenu* m_sortByMenu = nullptr;
    QActionGroup* m_sortCriterionActionGroup = nullptr;
    QList<QAction*> m_sortCriterionActions;
    QActionGroup* m_sortOrderActionGroup = nullptr;
    QAction* m_ascendingAction = nullptr;
    QAction* m_descendingAction = nullptr;

    // Context-menu resolution mode toggle (Architecture.md §14.13), persisted via QSettings under
    // WorkspacePaneWidget::kShowShellExtensionsSettingsKey — WorkspacePaneWidget reads the same
    // key independently at build-menu time, so no direct wiring exists between this action and
    // any pane.
    QAction* m_extendedContextMenuAction = nullptr;

    // Hidden files/folders visibility toggle (Architecture.md §14.21), persisted via QSettings
    // under FileListModel::kShowHiddenFilesSettingsKey. Unlike the context-menu toggle above, this
    // one is live-broadcast to every already-open tab's models on toggle, since hidden state
    // affects an already-populated model rather than something read fresh at next use.
    QAction* m_showHiddenFilesAction = nullptr;

    // Details-view column widths (Architecture.md §14.14): loaded from AppConfig, handed to
    // WorkspaceLayoutWidget so every pane's FileBrowserView starts out sized this way, then
    // refreshed from the focused pane's live widths in closeEvent() right before saving.
    std::array<int, FileListModel::ColumnCount> m_columnWidths{};

    // Details-view column order (Architecture.md §14.14), same load/refresh/save lifecycle as
    // m_columnWidths above.
    std::array<int, FileListModel::ColumnCount> m_columnOrder{};
};
