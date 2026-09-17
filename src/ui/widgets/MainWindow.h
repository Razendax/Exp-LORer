#pragma once

#include <array>

#include <QList>
#include <QMainWindow>

#include "AppConfig.h"
#include "FileListModel.h"
#include "FileNavigationUseCase.h"
#include "SplitLayout.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QMenu;
class QLineEdit;
class QToolBar;
class AppConfigStore;
class TagManagementUseCase;
class WorkspaceController;
class WorkspaceLayoutWidget;
class TagPanelWidget;
class TabViewModel;

// Application shell: menu bar (File/Edit/View/Favorites/Tools/Help), a small Layout toolbar, and
// a WorkspaceLayoutWidget as central widget hosting up to 4 independent WorkspacePaneWidget
// mini-browsers per the active SplitLayout (Architecture.md §14). Each pane owns its own
// tabs/navigation toolbar/view mode — MainWindow itself no longer holds any of that state.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(WorkspaceController* workspaceController, AppConfigStore& configStore, TagManagementUseCase& tagManagementUseCase,
               const AppConfig& initialConfig, QWidget* parent = nullptr);

protected:
    // Captures window geometry + WorkspaceController::captureConfig() and saves it via
    // m_configStore before chaining to QMainWindow::closeEvent (Architecture.md §14.14 — the only
    // place a save happens; no periodic/live autosave).
    void closeEvent(QCloseEvent* event) override;

    // Watches m_searchBar for Escape (clears the box and exits search on the focused tab).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createLayoutActions();
    void createSortByActions();
    void createContextMenuModeAction();
    void createMenuBar();
    void createLayoutToolBar();
    void createSearchBar(QToolBar* toolBar);
    void createWorkspace();
    void showTagManagerDialog();

    void onLayoutChanged(SplitLayout layout);
    void onStatusMessage(const QString& message);
    void onFocusedTabChanged(TabViewModel* tab);
    void onSortOrderChanged(SortCriterion criterion, bool ascending);
    void onSearchBarReturnPressed();
    void onSearchBarTextEdited(const QString& text);

    WorkspaceController* m_workspaceController = nullptr;
    AppConfigStore& m_configStore;
    TagManagementUseCase& m_tagManagementUseCase;
    WorkspaceLayoutWidget* m_workspaceLayoutWidget = nullptr;
    TagPanelWidget* m_tagPanelWidget = nullptr;
    TabViewModel* m_sortTrackedTab = nullptr;
    TabViewModel* m_searchTrackedTab = nullptr;
    QLineEdit* m_searchBar = nullptr;
    QAction* m_advancedSearchAction = nullptr;

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

    // Details-view column widths (Architecture.md §14.14): loaded from AppConfig, handed to
    // WorkspaceLayoutWidget so every pane's FileBrowserView starts out sized this way, then
    // refreshed from the focused pane's live widths in closeEvent() right before saving.
    std::array<int, FileListModel::ColumnCount> m_columnWidths{};
};
