#pragma once

#include <array>
#include <filesystem>

#include <QObject>

#include "SplitLayout.h"
#include "WorkspaceConfig.h"
#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class WorkspacePaneViewModel;
class TabViewModel;
class TagListViewModel;
class FileOperationsController;

// Mediator owning all 4 WorkspacePaneViewModels (always instantiated, regardless of which are
// currently visible under the active SplitLayout) plus the active SplitLayout, the focused pane,
// and the shared TagListViewModel (Architecture.md §14, §14.9). Lives in src/adapters, so it
// builds its panes directly in its constructor rather than going through CompositionRoot/src/app.
//
// Fulfills the §14.3/§14.8 retargeting hook: connects to focusedPaneChanged() and each pane's
// activeTabChanged() to retarget the shared TagListViewModel at the focused pane's active tab.
class WorkspaceController : public QObject
{
    Q_OBJECT

public:
    WorkspaceController(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                         QObject* parent = nullptr);

    WorkspacePaneViewModel* pane(WorkspacePaneId id) const;
    SplitLayout layout() const noexcept { return m_layout; }
    WorkspacePaneId focusedPane() const noexcept { return m_focusedPane; }
    TabViewModel* focusedTab() const;
    TagListViewModel* tagListViewModel() const noexcept { return m_tagListViewModel; }
    FileOperationsController* fileOperationsController() const noexcept { return m_fileOperationsController; }

    // Snapshots the current layout/focused pane and every pane's ordered tabs (path, ViewMode,
    // sort criterion/direction, active tab index) for persistence (Architecture.md §14.14).
    WorkspaceConfig captureConfig() const;

    // Rebuilds every pane's tabs from a saved WorkspaceConfig via the existing addTab()/
    // navigateTo()/setViewMode()/setSortCriterion() calls, then applies the saved active tab per
    // pane, layout, and focused pane. Returns whether at least one tab was restored, so the caller
    // can fall back to the fresh-install home-directory seed when nothing was.
    bool restoreFromConfig(const WorkspaceConfig& config);

public slots:
    void setLayout(SplitLayout layout);
    void setFocusedPane(WorkspacePaneId id);

signals:
    void layoutChanged(SplitLayout layout);
    void focusedPaneChanged(WorkspacePaneId id);
    void focusedTabChanged(TabViewModel* tab);

private:
    void retargetFocusedTab();

    // Connected to m_fileOperationsController->directoryContentsMayHaveChanged. Refreshes every
    // live tab, in any pane (visible or hidden), whose currentPath() matches directory, so a
    // paste/delete affecting a directory open in a different pane than the one it was triggered
    // from is reflected everywhere (Architecture.md §14.10).
    void refreshTabsShowing(const std::filesystem::path& directory);

    FileNavigationUseCase& m_fileNavigationUseCase;
    std::array<WorkspacePaneViewModel*, 4> m_panes;
    SplitLayout m_layout = SplitLayout::Single;
    WorkspacePaneId m_focusedPane = WorkspacePaneId::PaneA;
    TagListViewModel* m_tagListViewModel = nullptr;
    FileOperationsController* m_fileOperationsController = nullptr;
};
