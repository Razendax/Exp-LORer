#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

#include "FileListModel.h"
#include "ViewMode.h"

class QTabWidget;
class QLineEdit;
class QAction;
class QActionGroup;
class QToolBar;
class QStackedWidget;
class FileBrowserView;
class WorkspacePaneViewModel;
class TabViewModel;
class FileOperationsController;
class AddressBarWidget;

// One pane's full self-contained UI: its own toolbar (back/forward/up QActions, address
// QLineEdit, view-mode QToolButton+menu — the per-pane counterpart of MainWindow's former
// single-pane navigation toolbar, with its own QActionGroup instance) plus its own QTabWidget
// (closable tabs, a "+" corner button to add a tab), one FileBrowserView page per TabViewModel
// owned by the bound WorkspacePaneViewModel. Rebinds the toolbar/address bar to whichever
// TabViewModel is the current tab whenever the tab strip's current index changes.
class WorkspacePaneWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WorkspacePaneWidget(WorkspacePaneViewModel* pane, FileOperationsController* fileOperationsController,
                                  const std::array<int, FileListModel::ColumnCount>& initialColumnWidths = {},
                                  QWidget* parent = nullptr);

    WorkspacePaneViewModel* pane() const noexcept { return m_pane; }

    // Details-view column widths of whichever FileBrowserView the pane's current tab is currently
    // showing (Architecture.md §14.14), falling back to initialColumnWidths if the current tab's
    // stack page can't be resolved to a FileBrowserView (e.g. the pane has no tabs at all).
    std::array<int, FileListModel::ColumnCount> currentColumnWidths() const;

    // Shared with MainWindow's View-menu "Show shell extensions in context menu" QSettings-backed
    // toggle (Architecture.md §14.13) — read independently by each pane at build-menu time rather
    // than wired directly to that QAction, so the Application layer stays agnostic of where the
    // preference is stored.
    static constexpr const char* kShowShellExtensionsSettingsKey = "ContextMenu/ShowShellExtensions";

signals:
    // Bubbled up so MainWindow (the only widget with a shared status bar) can report it.
    void navigationFailed(const QString& message);

private:
    void createViewModeActions();
    QToolBar* createToolBar();
    void createTabArea();

    void addPageForTab(TabViewModel* tab, int index);
    void wireBrowserView(FileBrowserView* browserView, TabViewModel* tab);
    void bindToolBarToTab(TabViewModel* tab);
    int indexOfTab(TabViewModel* tab) const;

    void onTabAdded(int index);
    void onTabClosed(int index);
    void onActiveTabChanged(int index);
    void onTabWidgetCurrentChanged(int index);
    void onNewTabRequested();

    void onCurrentPathChanged(const std::filesystem::path& path);
    void onAddressBarEdited();
    void onAddressBarFolderSuggestionsRequested(const std::filesystem::path& directory);
    void onNavigationFailed(const std::filesystem::path& path, const QString& message);
    void onViewModeChanged(ViewMode mode);
    void onDeleteRequested(TabViewModel* tab, bool permanent);

    void showItemContextMenu(TabViewModel* tab, FileBrowserView* browserView, const std::vector<std::filesystem::path>& paths,
                              const QPoint& globalPos);
    void showBackgroundContextMenu(TabViewModel* tab, FileBrowserView* browserView, const QPoint& globalPos);
    static bool extendedShellExtensionsEnabled();

    WorkspacePaneViewModel* m_pane = nullptr;
    FileOperationsController* m_fileOperationsController = nullptr;
    TabViewModel* m_boundTab = nullptr;

    QTabWidget* m_tabWidget = nullptr;
    AddressBarWidget* m_addressBar = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_upAction = nullptr;

    // One QAction per ViewMode (same order as the anonymous-namespace kViewModes array in the
    // .cpp), shared between this pane's toolbar dropdown and (eventually) its own view menu.
    QActionGroup* m_viewModeActionGroup = nullptr;
    QList<QAction*> m_viewModeActions;

    // Applied to every FileBrowserView this pane creates (addPageForTab), and the fallback for
    // currentColumnWidths() when there's no current tab to read live widths from.
    std::array<int, FileListModel::ColumnCount> m_initialColumnWidths{};
};
