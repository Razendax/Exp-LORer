#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

#include "FileListModel.h"
#include "GitRepositorySummary.h"
#include "ViewMode.h"

class QTabWidget;
class QLineEdit;
class QAction;
class QActionGroup;
class QToolBar;
class QToolButton;
class QStackedWidget;
class FileBrowserView;
class WorkspacePaneViewModel;
class TabViewModel;
class FileOperationsController;
class AddressBarWidget;
class MediaViewerSortViewBar;
class BottomPanelWidget;
class StatusBarWidget;
class ColumnVisibilitySettingsViewModel;
class DiskUsageService;
class FragmentationService;
class NotificationCenter;
class UiThemeChangeNotifier;
class HotkeyManager;

// One pane's full self-contained UI: its own toolbar (back/forward/up/refresh QActions, address
// QLineEdit, view-mode QToolButton+menu — the per-pane counterpart of MainWindow's former
// single-pane navigation toolbar, with its own QActionGroup instance) plus its own QTabWidget
// (closable tabs; the blank tab-bar space after the last tab -- always at least
// kNewTabButtonReservedWidth wide, however many tabs are open -- is itself a borderless button
// that adds a tab, see m_newTabButton), one FileBrowserView page per TabViewModel owned by the
// bound WorkspacePaneViewModel. Rebinds the toolbar/address bar to whichever TabViewModel is the
// current tab whenever the tab strip's current index changes.
class WorkspacePaneWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WorkspacePaneWidget(WorkspacePaneViewModel* pane, FileOperationsController* fileOperationsController,
                                  ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel,
                                  DiskUsageService& diskUsageService, FragmentationService& fragmentationService,
                                  NotificationCenter& notificationCenter, UiThemeChangeNotifier& uiThemeChangeNotifier,
                                  HotkeyManager& hotkeyManager,
                                  const std::array<int, FileListModel::ColumnCount>& initialColumnWidths = {},
                                  const std::array<int, FileListModel::ColumnCount>& initialColumnOrder = {},
                                  QWidget* parent = nullptr);

    WorkspacePaneViewModel* pane() const noexcept { return m_pane; }

    // Navigates the bound tab to `directory` (through the same confirmLeavingMonitoredScope guard
    // every other navigation entry point uses), then -- if navigation landed there and
    // entryToSelect is set -- selects that entry in the resulting listing. Backs the Favorites menu
    // (Architecture.md §14.35): a folder favorite passes std::nullopt, a file favorite passes its
    // own path with directory = its parent.
    void navigateAndSelect(const std::filesystem::path& directory, const std::optional<std::filesystem::path>& entryToSelect);

    // Catches middle-click on the tab bar to close a tab even when the close button is hidden
    // (setTabCloseButtonsVisible(false)) -- QTabBar has no built-in middle-click-to-close signal.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Details-view column widths of whichever FileBrowserView the pane's current tab is currently
    // showing (Architecture.md §14.14), falling back to initialColumnWidths if the current tab's
    // stack page can't be resolved to a FileBrowserView (e.g. the pane has no tabs at all).
    std::array<int, FileListModel::ColumnCount> currentColumnWidths() const;

    // Details-view column order counterpart of currentColumnWidths() above (Architecture.md §14.14).
    std::array<int, FileListModel::ColumnCount> currentColumnOrder() const;

    // General > UI "Show close button on tabs" toggle (SettingsDialog), persisted via QSettings and
    // propagated live to every open pane by MainWindow::showSettingsDialog() -- same posture as the
    // hidden-files toggle in MainWindow::createShowHiddenFilesAction.
    static constexpr const char* kShowTabCloseButtonsSettingsKey = "UI/ShowTabCloseButtons";
    static bool tabCloseButtonsEnabled();
    void setTabCloseButtonsVisible(bool visible);

    // Focused-pane accent (WorkspaceLayoutWidget, driven by WorkspaceController::focusedPaneChanged):
    // toggles the "paneActive" dynamic property on this pane's toolbar and forces a restyle.
    void setActive(bool active);

signals:
    // Bubbled up so MainWindow (whose QMainWindow::statusBar() is a separate, transient,
    // window-wide message bar for navigation/search failures -- distinct from this pane's own
    // always-visible StatusBarWidget, Architecture.md §14.27) can report it.
    void navigationFailed(const QString& message);

    // Same bubble-up posture as navigationFailed, for git checkout failures (Architecture.md
    // §14.34).
    void gitOperationFailed(const QString& message);

    // "Bulk Rename..." on the item context menu (Architecture.md §14.55), bubbled up so MainWindow
    // can build the transient BulkRenameViewModel/BulkRenameDialog pair -- same bubble-up posture as
    // navigationFailed.
    void bulkRenameRequested(TabViewModel* tab, const std::vector<std::filesystem::path>& paths);

private:
    void createViewModeActions();
    QToolBar* createToolBar();
    void createTabArea();

    // Re-applies m_toolBar's and m_tabWidget's UiColors::current()-based stylesheets -- extracted
    // out of createToolBar()/createTabArea() so it can be re-invoked on
    // UiThemeChangeNotifier::themeChanged without rebuilding either widget (Architecture.md §14.50).
    void applyThemeColors();

    void addPageForTab(TabViewModel* tab, int index);
    void wireBrowserView(FileBrowserView* browserView, TabViewModel* tab);

    // Caps the tab bar's own maximumWidth to tabWidgetWidth - kNewTabButtonReservedWidth (so its
    // scroll buttons, not m_newTabButton's reserved strip, absorb any overflow), then repositions
    // m_newTabButton to exactly cover the blank tab-bar space from the tab bar's new right edge to
    // the tab widget's right edge. Called whenever either might have changed width (tabBar and
    // tabWidget QEvent::Resize, caught in eventFilter).
    void updateNewTabButtonGeometry();

    // Applies m_columnVisibilitySettingsViewModel's current defaults (read live, not a copied
    // snapshot) to a freshly created FileBrowserView -- called once per view from addPageForTab.
    void applyDefaultColumnVisibility(FileBrowserView* browserView);

    // Overrides applyDefaultColumnVisibility's result with `tab`'s own restored per-tab choices
    // (Architecture.md §14.49) -- called right after it from addPageForTab, for each column where
    // tab->hiddenColumns() has an explicit (non-nullopt) entry.
    void applyRestoredColumnVisibility(FileBrowserView* browserView, TabViewModel* tab);
    void bindToolBarToTab(TabViewModel* tab);
    int indexOfTab(TabViewModel* tab) const;

    // Media Viewer mode's navigation lock (Architecture.md §14.61): disables back/forward/up/
    // refresh/address bar/view-mode button while active, restoring them (back/forward/up to
    // m_boundTab's own availability, not unconditionally true) when it isn't. Invoked both by the
    // bound tab's live TabMediaViewerViewModel::modeChanged signal and by bindToolBarToTab on every
    // tab switch.
    void updateToolBarEnabledForMediaViewer(bool active);

    // Folder-Grouped Browsing (Architecture.md §14.74): true exactly when m_boundTab is in Media
    // Viewer mode with Folder group mode + Thumbnails view both active -- the one combination
    // where back/forward/up/address-bar act on the folder-nav state instead of being disabled.
    bool mediaViewerFolderNavActive() const;

    // Folder-Grouped Browsing (Architecture.md §14.74): re-runs updateToolBarEnabledForMediaViewer()
    // against the bound tab's current TabMediaViewerViewModel::isActive() state. Connected to
    // groupModeChanged/viewModeChanged/back|forward|upAvailableChanged -- any of those can change
    // whether folder-nav is active or what it's enabled against, even while modeChanged itself
    // (Media Viewer on/off) hasn't fired.
    void onMediaViewerFolderNavChanged();

    // Resolves the FileBrowserView backing whichever page (normal browsing, search results, or
    // advanced search results) the current tab is currently showing -- factors out the splitter ->
    // QStackedWidget -> (FileBrowserView or SearchResultsPane::browserView()) unwrap also used by
    // currentColumnWidths(). Returns nullptr if it can't be resolved (e.g. no tabs at all).
    FileBrowserView* currentBrowserView() const;

    void onTabAdded(int index);
    void onTabClosed(int index);
    void onActiveTabChanged(int index);
    void onTabWidgetCurrentChanged(int index);
    void onNewTabRequested();
    void onTabCloseRequested(int index);

    void onCurrentPathChanged(const std::filesystem::path& path);
    void onAddressBarEdited();
    void onAddressBarFolderSuggestionsRequested(const std::filesystem::path& directory);
    void onNavigationFailed(const std::filesystem::path& path, const QString& message);
    void onViewModeChanged(ViewMode mode);
    void onDeleteRequested(TabViewModel* tab, bool permanent);

    // Shared by the toolbar's Back/Forward/Up QActions and FileBrowserView's corresponding
    // navigateBackRequested/navigateForwardRequested/navigateUpRequested signals -- act on
    // m_boundTab, guarded by confirmLeavingMonitoredScope.
    void onUpRequested();
    void onBackRequested();
    void onForwardRequested();

    // Returns true if the navigation should proceed (either it stays within the monitored
    // subtree, or the user confirmed stopping monitoring). Stops monitoring itself on a confirmed
    // "yes" before returning, so callers just need: if (!confirmLeavingMonitoredScope(tab,
    // target)) return;
    bool confirmLeavingMonitoredScope(TabViewModel* tab, const std::filesystem::path& target);

    // Status bar (Architecture.md §14.27): reads counts directly off m_boundTab rather than off a
    // signal payload, so it's correct regardless of which of the six wired signals fired.
    void refreshStatusCounts();
    void onQuickSelectTextChanged(const QString& text);

    // Git branch indicator/checkout (Architecture.md §14.34).
    void updateGitStatusBar(const GitRepositorySummary& summary);
    void onBranchButtonClicked();
    void onBranchSelected(const QString& branchName);

    void showItemContextMenu(TabViewModel* tab, FileBrowserView* browserView, const std::vector<std::filesystem::path>& paths,
                              const QPoint& globalPos);
    void showBackgroundContextMenu(TabViewModel* tab, FileBrowserView* browserView, const QPoint& globalPos);

    WorkspacePaneViewModel* m_pane = nullptr;
    FileOperationsController* m_fileOperationsController = nullptr;
    ColumnVisibilitySettingsViewModel& m_columnVisibilitySettingsViewModel;
    DiskUsageService& m_diskUsageService;
    FragmentationService& m_fragmentationService;
    NotificationCenter& m_notificationCenter;
    UiThemeChangeNotifier& m_uiThemeChangeNotifier;
    HotkeyManager& m_hotkeyManager;
    TabViewModel* m_boundTab = nullptr;
    // Every connection bindToolBarToTab() makes to m_boundTab, so unbinding is just "disconnect
    // everything in this list" instead of a hand-maintained disconnect() call paired to each
    // connect() call -- that pairing had nothing enforcing it stayed correct as connections were
    // added over time.
    QList<QMetaObject::Connection> m_tabConnections;

    QTabWidget* m_tabWidget = nullptr;
    // Blank, borderless button manually positioned (not a QTabWidget corner widget -- corner
    // widgets don't reliably keep their reserved width once the tab bar's own scroll buttons
    // engage, see the comment in createTabArea()) over the empty tab-bar strip after the last tab
    // -- kNewTabButtonReservedWidth wide at minimum, guaranteed by capping the tab bar's own
    // maximumWidth in updateNewTabButtonGeometry() -- so pressing anywhere in that "no tab here"
    // space adds a tab. Styled to read as ordinary tab-bar space rather than a button (no
    // hover/pressed/focus chrome).
    QToolButton* m_newTabButton = nullptr;
    QToolBar* m_toolBar = nullptr;
    AddressBarWidget* m_addressBar = nullptr;
    // Media Viewer mode's toolbar sort/view controls (Architecture.md §14.68), added right after
    // m_addressBar; visible/enabled only while Media Viewer mode is active.
    MediaViewerSortViewBar* m_mediaViewerSortViewBar = nullptr;
    // The QWidgetAction QToolBar::addWidget() implicitly created for m_mediaViewerSortViewBar.
    // Visibility is toggled through this action (not the widget's own setVisible()) -- QToolBar's
    // internal layout keys its show/hide and space-reservation decisions off the action's visible
    // property, not the embedded widget's, so toggling the widget directly leaves stale layout
    // space in place.
    QAction* m_mediaViewerSortViewBarAction = nullptr;
    StatusBarWidget* m_statusBar = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_upAction = nullptr;
    QAction* m_refreshAction = nullptr;
    // Disabled/enabled alongside the actions above by updateToolBarEnabledForMediaViewer
    // (Architecture.md §14.61) -- otherwise unowned by any member (createToolBar() only needed a
    // local), but Media Viewer mode's navigation lock needs to reach it too.
    QToolButton* m_viewModeButton = nullptr;

    // One QAction per ViewMode (same order as the anonymous-namespace kViewModes array in the
    // .cpp), shared between this pane's toolbar dropdown and (eventually) its own view menu.
    QActionGroup* m_viewModeActionGroup = nullptr;
    QList<QAction*> m_viewModeActions;

    // Applied to every FileBrowserView this pane creates (addPageForTab), and the fallback for
    // currentColumnWidths() when there's no current tab to read live widths from.
    std::array<int, FileListModel::ColumnCount> m_initialColumnWidths{};

    // Applied to every FileBrowserView this pane creates (addPageForTab), and the fallback for
    // currentColumnOrder() when there's no current tab to read live order from.
    std::array<int, FileListModel::ColumnCount> m_initialColumnOrder{};
};
