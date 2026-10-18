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
#include "WorkspaceEntry.h"
#include "WorkspaceLaunchContext.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QResizeEvent;
class QMenu;
class AppConfigStore;
class HighlightThemeViewModel;
class FileDecorationsViewModel;
class FavoritesViewModel;
class EnvironmentVariablesViewModel;
class NotificationCenter;
class NotificationSettingsViewModel;
class ColumnVisibilitySettingsViewModel;
class SyntaxHighlightEngine;
class TagManagementUseCase;
class TagChangeNotifier;
class AdvancedSearchService;
class IconLoadingService;
class FileTagsService;
class UiThemeChangeNotifier;
class WorkspaceStore;
class WorkspaceController;
class WorkspaceLayoutWidget;
class TagPanelWidget;
class SidePanelWidget;
class FolderTreeView;
class SearchPanelWidget;
class MediaViewerSearchPanelWidget;
class PreviewPanelWidget;
class MonitoringPanelWidget;
class NotificationOverlayWidget;
class TabViewModel;
class MainStatusBarWidget;
class DriveSpaceInfoWorker;
class DiskUsageService;
class DuplicateFinderService;
class DuplicateFinderDialog;
class FragmentationService;
class BulkRenameUseCase;

// Application shell: menu bar (File/Edit/View/Favorites/Tools/Help), a small Layout toolbar, and
// a WorkspaceLayoutWidget as central widget hosting up to 4 independent WorkspacePaneWidget
// mini-browsers per the active SplitLayout (Architecture.md §14). Each pane owns its own
// tabs/navigation toolbar/view mode — MainWindow itself no longer holds any of that state.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(WorkspaceController* workspaceController, AppConfigStore& configStore, TagManagementUseCase& tagManagementUseCase,
               TagChangeNotifier& tagChangeNotifier, AdvancedSearchService& advancedSearchService,
               IconLoadingService& iconLoadingService, FileTagsService& fileTagsService,
               UiThemeChangeNotifier& uiThemeChangeNotifier,
               SyntaxHighlightEngine& syntaxHighlightEngine,
               HighlightThemeViewModel& highlightThemeViewModel, FileDecorationsViewModel& fileDecorationsViewModel,
               FavoritesViewModel& favoritesViewModel, NotificationCenter& notificationCenter,
               NotificationSettingsViewModel& notificationSettingsViewModel,
               ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, DiskUsageService& diskUsageService,
               DuplicateFinderService& duplicateFinderService, FragmentationService& fragmentationService,
               BulkRenameUseCase& bulkRenameUseCase, EnvironmentVariablesViewModel& environmentVariablesViewModel,
               WorkspaceStore& workspaceStore, const WorkspaceLaunchContext& launchContext, const AppConfig& initialConfig,
               QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    // Captures window geometry + WorkspaceController::captureConfig() and saves it via
    // m_configStore before chaining to QMainWindow::closeEvent (Architecture.md §14.14 — the only
    // place a save happens; no periodic/live autosave).
    void closeEvent(QCloseEvent* event) override;

    // Repositions m_notificationOverlayWidget to the bottom-right corner on every resize.
    void resizeEvent(QResizeEvent* event) override;

private:
    void createLayoutActions();
    void createSortByActions();
    void createContextMenuModeAction();
    void createShowHiddenFilesAction();
    void createMediaViewerModeAction();
    void createHoverPreviewAction();
    void createMenuBar();
    void createFavoritesMenu();
    void createWorkspacesMenu(QMenu* fileMenu);
    void createLayoutToolBar();
    void createWorkspace();
    void createFolderTree();
    void createSearchPanel();
    void createMainStatusBar();
    void showTagManagerDialog();
    void showSettingsDialog();
    void showManageFavoritesDialog();
    void showDuplicateFinderDialog();
    void showEnvironmentVariablesDialog();
    void showTextEditorDialog();

    // Workspaces (File menu, Architecture.md §14.60).
    void rebuildWorkspacesMenuItems();
    void launchOrActivateWorkspace(const WorkspaceEntry& entry);
    void onSaveAsWorkspaceTriggered();
    void showWorkspaceManagerDialog();
    QString currentWorkspaceName() const;

    // Factored out of closeEvent() so the new "Save as Workspace..." action can capture the exact
    // same AppConfig snapshot without duplicating it (Architecture.md §14.60). Unlike the inline
    // code it replaces, this never mutates m_columnWidths/m_columnOrder -- nothing read those
    // members again afterward anyway, so the old mutation was side-effect-free busywork.
    AppConfig captureCurrentConfig() const;

    // Tools > Bulk Rename... (Architecture.md §14.55): uses the focused tab's current selection if
    // it has 2+ items, otherwise falls back to the whole current-folder listing.
    void showBulkRenameDialogFromMenu();

    // Converges both entry points (the item context menu's "Bulk Rename..." and the Tools-menu
    // fallback above) on building and exec()ing the transient BulkRenameViewModel/BulkRenameDialog
    // pair for `selection`. No-op for an empty selection.
    void showBulkRenameDialogFor(const std::vector<FileNode>& selection);

    void onLayoutChanged(SplitLayout layout);
    void onStatusMessage(const QString& message);
    void onFocusedTabChanged(TabViewModel* tab);

    // Media Viewer mode (Architecture.md §14.61): shows/hides the left panel and the right panel's
    // Tags/Preview/Monitoring/Searcher tab buttons, and switches the right panel to the Searcher
    // tab, in response to the focused tab's own TabMediaViewerViewModel::modeChanged.
    void onMediaViewerModeChanged(bool active);

    // Resolves which tab Media-Viewer-related actions (the toggle action, the Searcher panel's
    // exit/kind/filter/tag controls) should act on: the tab actually in Media Viewer mode
    // (m_activeMediaViewerTab) when one exists, since focus may have moved elsewhere in a split
    // workspace; otherwise the currently-focused tab (e.g. for entering Media Viewer mode).
    TabViewModel* mediaViewerTargetTab() const;
    void onSortOrderChanged(SortCriterion criterion, bool ascending);
    void onActiveTabPathChanged(const std::filesystem::path& path);
    void onDriveSpaceInfoReady(int generation, Result<DriveSpaceInfo> result);

    // Navigates the focused pane's active tab to `path` (through WorkspacePaneWidget's
    // monitored-scope guard) -- the left panel's folder tree click/Enter/"Open" (Architecture.md
    // §14.41).
    void navigateFocusedPaneTo(const std::filesystem::path& path);

    // Search tab result activation (Architecture.md §14.42): a file navigates to its parent
    // folder with the file selected, a folder navigates into it -- same two forms as
    // onFavoriteItemTriggered's file/folder favorite handling, via the same navigateAndSelect entry
    // point.
    void onSearchResultActivated(const std::filesystem::path& path, bool isDirectory);

    // Favorites menu (Architecture.md §14.35).
    void onAddToFavoritesTriggered();
    void onFavoriteItemTriggered(Favorites::Id id);
    void rebuildFavoritesMenuItems();
    void updateFavoritesActionEnabled();
    void onFavoritesTrackedSelectionChanged(const std::vector<FileNode>& entries);
    void onFavoritesTrackedPathChanged(const std::filesystem::path& path);

    // Bubbled up from WorkspacePaneWidget::bulkRenameRequested (Architecture.md §14.55) -- resolves
    // `paths` back to FileNode via tab->selectedEntries() (the exact list they were built from) and
    // calls showBulkRenameDialogFor directly, no fallback needed since the context-menu entry is
    // only enabled for 2+ items.
    void onBulkRenameRequested(TabViewModel* tab, const std::vector<std::filesystem::path>& paths);

    WorkspaceController* m_workspaceController = nullptr;
    AppConfigStore& m_configStore;
    TagManagementUseCase& m_tagManagementUseCase;
    TagChangeNotifier& m_tagChangeNotifier;
    AdvancedSearchService& m_advancedSearchService;
    IconLoadingService& m_iconLoadingService;
    FileTagsService& m_fileTagsService;
    UiThemeChangeNotifier& m_uiThemeChangeNotifier;
    SyntaxHighlightEngine& m_syntaxHighlightEngine;
    HighlightThemeViewModel& m_highlightThemeViewModel;
    FileDecorationsViewModel& m_fileDecorationsViewModel;
    FavoritesViewModel& m_favoritesViewModel;
    NotificationCenter& m_notificationCenter;
    NotificationSettingsViewModel& m_notificationSettingsViewModel;
    ColumnVisibilitySettingsViewModel& m_columnVisibilitySettingsViewModel;
    DiskUsageService& m_diskUsageService;
    DuplicateFinderService& m_duplicateFinderService;
    FragmentationService& m_fragmentationService;
    BulkRenameUseCase& m_bulkRenameUseCase;
    EnvironmentVariablesViewModel& m_environmentVariablesViewModel;
    WorkspaceStore& m_workspaceStore;
    WorkspaceLaunchContext m_launchContext;
    DuplicateFinderDialog* m_duplicateFinderDialog = nullptr;
    WorkspaceLayoutWidget* m_workspaceLayoutWidget = nullptr;
    TagPanelWidget* m_tagPanelWidget = nullptr;
    PreviewPanelWidget* m_previewPanelWidget = nullptr;
    MonitoringPanelWidget* m_monitoringPanelWidget = nullptr;
    NotificationOverlayWidget* m_notificationOverlayWidget = nullptr;
    SidePanelWidget* m_rightPanelWidget = nullptr;
    SidePanelWidget* m_leftPanelWidget = nullptr;
    FolderTreeView* m_folderTreeView = nullptr;
    SearchPanelWidget* m_searchPanelWidget = nullptr;
    MediaViewerSearchPanelWidget* m_mediaViewerSearchPanelWidget = nullptr;
    TabViewModel* m_sortTrackedTab = nullptr;
    TabViewModel* m_searchTrackedTab = nullptr;
    TabViewModel* m_mediaViewerTrackedTab = nullptr;
    // The tab actually in Media Viewer mode, independent of which tab/pane currently has focus --
    // set when its TabMediaViewerViewModel::modeChanged(true) fires, cleared on modeChanged(false).
    // In a split workspace, focus can move to a different tab while this one still shows Media
    // Viewer mode (forced single-pane); mediaViewerTargetTab() is what every Media-Viewer action
    // (toggle, exit, kind/filter changes from the Searcher panel) should resolve its target tab
    // through, instead of re-deriving m_workspaceController->focusedTab() live.
    TabViewModel* m_activeMediaViewerTab = nullptr;
    TabViewModel* m_driveInfoTrackedTab = nullptr;
    MainStatusBarWidget* m_mainStatusBarWidget = nullptr;

    // Favorites menu (Architecture.md §14.35): rebuilt lazily on QMenu::aboutToShow rather than
    // kept live-synced, so no direct favoritesChanged() wiring is needed here.
    QMenu* m_favoritesMenu = nullptr;
    QAction* m_addToFavoritesAction = nullptr;
    TabViewModel* m_favoritesTrackedTab = nullptr;

    // Workspaces submenu (Architecture.md §14.60): rebuilt lazily on QMenu::aboutToShow, same
    // posture as m_favoritesMenu above.
    QMenu* m_workspacesMenu = nullptr;

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

    // Media Viewer mode toggle (Architecture.md §14.61), wired to the focused tab's enterMediaViewer()/
    // mediaViewer()->exit() the same way the sort actions target focusedTab(). Not persisted (same
    // posture as m_showHiddenFilesAction is NOT -- this one specifically mirrors
    // m_searchTrackedTab's per-tab, not-restored-on-restart posture).
    QAction* m_mediaViewerModeAction = nullptr;

    // Hover preview popup toggle (Architecture.md §14.76), persisted via QSettings under
    // FileBrowserView::kHoverPreviewEnabledSettingsKey -- same "read fresh at next use" posture as
    // m_extendedContextMenuAction, not m_showHiddenFilesAction's live-broadcast one.
    QAction* m_hoverPreviewEnabledAction = nullptr;

    // Right panel's active tab index right before Media Viewer mode's Searcher tab took over, so
    // exiting the mode restores whichever tab (Tags/Preview/Monitoring) was showing before. -1 if
    // none was active (panel was collapsed).
    int m_rightPanelActiveIndexBeforeMediaViewer = -1;

    // Details-view column widths (Architecture.md §14.14): loaded from AppConfig, handed to
    // WorkspaceLayoutWidget so every pane's FileBrowserView starts out sized this way, then
    // refreshed from the focused pane's live widths in closeEvent() right before saving.
    std::array<int, FileListModel::ColumnCount> m_columnWidths{};

    // Details-view column order (Architecture.md §14.14), same load/refresh/save lifecycle as
    // m_columnWidths above.
    std::array<int, FileListModel::ColumnCount> m_columnOrder{};

    // Left/right side-panel expand/tab/width state (Architecture.md §14.14, §14.25/§14.41): read
    // from AppConfig in the constructor, applied once createWorkspace() builds the panels, and
    // captured back from the live widgets in closeEvent().
    SidePanelState m_initialLeftPanelState;
    SidePanelState m_initialRightPanelState;
};
