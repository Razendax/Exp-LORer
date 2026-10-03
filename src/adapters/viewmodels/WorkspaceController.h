#pragma once

#include <array>
#include <filesystem>

#include <QObject>

#include "SplitLayout.h"
#include "WorkspaceConfig.h"
#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class FilePreviewUseCase;
class IFileSystemRepository;
class FileDecorationRules;
class WorkspacePaneViewModel;
class TabViewModel;
class TagListViewModel;
class FilePreviewViewModel;
class FileOperationsController;
class FolderTreeModel;
class GitStatusService;
class DirectoryWatchUseCase;
class DirectoryWatchService;
class FolderMonitorUseCase;
class MonitoringPanelViewModel;
class NotificationCenter;
class AdvancedSearchService;
class FileTransferService;
class FileTagsService;
class DiskUsageService;
class TagChangeNotifier;

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
    // See TabViewModel's constructor comment for why fileDecorationsChangeSource is a bare QObject&.
    WorkspaceController(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                         FilePreviewUseCase& filePreviewUseCase, IFileSystemRepository& fileSystemRepository,
                         const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                         GitStatusService& gitStatusService, DirectoryWatchUseCase& directoryWatchUseCase,
                         FolderMonitorUseCase& folderMonitorUseCase, NotificationCenter& notificationCenter,
                         AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                         DiskUsageService& diskUsageService, TagChangeNotifier& tagChangeNotifier,
                         FileTransferService& fileTransferService, QObject* parent = nullptr);

    WorkspacePaneViewModel* pane(WorkspacePaneId id) const;
    SplitLayout layout() const noexcept { return m_layout; }
    WorkspacePaneId focusedPane() const noexcept { return m_focusedPane; }
    TabViewModel* focusedTab() const;
    TagListViewModel* tagListViewModel() const noexcept { return m_tagListViewModel; }
    FilePreviewViewModel* filePreviewViewModel() const noexcept { return m_filePreviewViewModel; }
    MonitoringPanelViewModel* monitoringPanelViewModel() const noexcept { return m_monitoringPanelViewModel; }
    FileOperationsController* fileOperationsController() const noexcept { return m_fileOperationsController; }
    FolderTreeModel* folderTreeModel() const noexcept { return m_folderTreeModel; }
    FileNavigationUseCase& fileNavigationUseCase() const noexcept { return m_fileNavigationUseCase; }

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

    // Connected to m_fileOperationsController->pathRenamed/pathRemoved (Architecture.md §14.41).
    // Every live tab, in any pane, whose currentPath() is the renamed/removed path or inside it is
    // moved -- to the rewritten path after a rename, to the removed path's parent after a delete --
    // replacing its current history entry rather than adding one.
    void onPathRenamed(const std::filesystem::path& source, const std::filesystem::path& destination);
    void onPathRemoved(const std::filesystem::path& path);

    // Recomputes the visible+active tab set from m_layout and each visible pane's activeTab(),
    // and hands it to m_directoryWatchService. Called whenever that set may have changed: after
    // construction, from setLayout(), and from any pane's activeTabChanged (Architecture.md
    // §14.x). Deliberately not triggered by focusedPaneChanged -- visibility, not focus, decides
    // what's live-watched.
    void updateLiveWatchedTabs();

    FileNavigationUseCase& m_fileNavigationUseCase;
    std::array<WorkspacePaneViewModel*, 4> m_panes;
    SplitLayout m_layout = SplitLayout::Single;
    WorkspacePaneId m_focusedPane = WorkspacePaneId::PaneA;
    TagListViewModel* m_tagListViewModel = nullptr;
    FilePreviewViewModel* m_filePreviewViewModel = nullptr;
    MonitoringPanelViewModel* m_monitoringPanelViewModel = nullptr;
    FileOperationsController* m_fileOperationsController = nullptr;
    FolderTreeModel* m_folderTreeModel = nullptr;
    DirectoryWatchService* m_directoryWatchService = nullptr;
    TagChangeNotifier& m_tagChangeNotifier;
};
