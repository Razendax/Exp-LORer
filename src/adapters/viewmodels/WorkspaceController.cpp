#include "WorkspaceController.h"

#include <vector>

#include "DirectoryWatchService.h"
#include "FileOperationsController.h"
#include "FilePreviewViewModel.h"
#include "FolderTreeModel.h"
#include "MonitoringPanelViewModel.h"
#include "PathContainment.h"
#include "TabViewModel.h"
#include "TagListViewModel.h"
#include "WorkspaceLayoutTopology.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    int indexOf(WorkspacePaneId id)
    {
        return static_cast<int>(id);
    }
}

WorkspaceController::WorkspaceController(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                                           FilePreviewUseCase& filePreviewUseCase, IFileSystemRepository& fileSystemRepository,
                                           const FileDecorationRules& fileDecorationRules,
                                           QObject& fileDecorationsChangeSource, GitStatusService& gitStatusService,
                                           DirectoryWatchUseCase& directoryWatchUseCase,
                                           FolderMonitorUseCase& folderMonitorUseCase, NotificationCenter& notificationCenter,
                                           AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                                           DiskUsageService& diskUsageService, TagChangeNotifier& tagChangeNotifier,
                                           FileTransferService& fileTransferService, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_tagChangeNotifier(tagChangeNotifier)
{
    m_panes[indexOf(WorkspacePaneId::PaneA)] = new WorkspacePaneViewModel(
        fileNavigationUseCase, tagManagementUseCase, fileDecorationRules, fileDecorationsChangeSource, gitStatusService,
        folderMonitorUseCase, advancedSearchService, fileTagsService, diskUsageService, tagChangeNotifier,
        WorkspacePaneId::PaneA, this);
    m_panes[indexOf(WorkspacePaneId::PaneB)] = new WorkspacePaneViewModel(
        fileNavigationUseCase, tagManagementUseCase, fileDecorationRules, fileDecorationsChangeSource, gitStatusService,
        folderMonitorUseCase, advancedSearchService, fileTagsService, diskUsageService, tagChangeNotifier,
        WorkspacePaneId::PaneB, this);
    m_panes[indexOf(WorkspacePaneId::PaneC)] = new WorkspacePaneViewModel(
        fileNavigationUseCase, tagManagementUseCase, fileDecorationRules, fileDecorationsChangeSource, gitStatusService,
        folderMonitorUseCase, advancedSearchService, fileTagsService, diskUsageService, tagChangeNotifier,
        WorkspacePaneId::PaneC, this);
    m_panes[indexOf(WorkspacePaneId::PaneD)] = new WorkspacePaneViewModel(
        fileNavigationUseCase, tagManagementUseCase, fileDecorationRules, fileDecorationsChangeSource, gitStatusService,
        folderMonitorUseCase, advancedSearchService, fileTagsService, diskUsageService, tagChangeNotifier,
        WorkspacePaneId::PaneD, this);

    m_tagListViewModel = new TagListViewModel(tagManagementUseCase, fileNavigationUseCase, notificationCenter, tagChangeNotifier, this);
    m_filePreviewViewModel = new FilePreviewViewModel(filePreviewUseCase, fileSystemRepository, this);
    m_monitoringPanelViewModel = new MonitoringPanelViewModel(this);
    m_fileOperationsController = new FileOperationsController(m_fileNavigationUseCase, fileTransferService, notificationCenter, this);
    m_directoryWatchService = new DirectoryWatchService(directoryWatchUseCase, this);
    m_folderTreeModel = new FolderTreeModel(fileNavigationUseCase, this);

    connect(this, &WorkspaceController::focusedPaneChanged, this, &WorkspaceController::retargetFocusedTab);
    for (WorkspacePaneViewModel* pane : m_panes)
    {
        connect(pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspaceController::retargetFocusedTab);
        connect(pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspaceController::updateLiveWatchedTabs);
    }

    connect(m_fileOperationsController, &FileOperationsController::directoryContentsMayHaveChanged, this,
            &WorkspaceController::refreshTabsShowing);
    connect(m_fileOperationsController, &FileOperationsController::directoryContentsMayHaveChanged, m_folderTreeModel,
            &FolderTreeModel::refreshDirectory);
    connect(m_fileOperationsController, &FileOperationsController::pathRenamed, this, &WorkspaceController::onPathRenamed);
    connect(m_fileOperationsController, &FileOperationsController::pathRemoved, this, &WorkspaceController::onPathRemoved);

    retargetFocusedTab();
    updateLiveWatchedTabs();
}

WorkspacePaneViewModel* WorkspaceController::pane(WorkspacePaneId id) const
{
    return m_panes[static_cast<size_t>(indexOf(id))];
}

TabViewModel* WorkspaceController::focusedTab() const
{
    return pane(m_focusedPane)->activeTab();
}

WorkspaceConfig WorkspaceController::captureConfig() const
{
    WorkspaceConfig config;
    config.layout = m_layout;
    config.focusedPane = m_focusedPane;

    for (WorkspacePaneId id : { WorkspacePaneId::PaneA, WorkspacePaneId::PaneB, WorkspacePaneId::PaneC, WorkspacePaneId::PaneD })
    {
        WorkspacePaneViewModel* paneViewModel = pane(id);
        PaneConfig& paneConfig = config.panes[static_cast<size_t>(indexOf(id))];
        paneConfig.activeTabIndex = paneViewModel->activeIndex();

        for (int i = 0; i < paneViewModel->tabCount(); ++i)
        {
            TabViewModel* tab = paneViewModel->tabAt(i);
            paneConfig.tabs.push_back(
                TabConfig{ tab->currentPath(), tab->viewMode(), tab->sortCriterion(), tab->sortAscending(), tab->hiddenColumns() });
        }
    }

    return config;
}

bool WorkspaceController::restoreFromConfig(const WorkspaceConfig& config)
{
    bool restoredAny = false;

    for (WorkspacePaneId id : { WorkspacePaneId::PaneA, WorkspacePaneId::PaneB, WorkspacePaneId::PaneC, WorkspacePaneId::PaneD })
    {
        const PaneConfig& paneConfig = config.panes[static_cast<size_t>(indexOf(id))];
        if (paneConfig.tabs.empty())
        {
            continue;
        }

        WorkspacePaneViewModel* paneViewModel = pane(id);
        for (const TabConfig& tabConfig : paneConfig.tabs)
        {
            TabViewModel* tab = paneViewModel->addTab();
            tab->navigateTo(tabConfig.path);
            tab->setViewMode(tabConfig.viewMode);
            tab->setSortCriterion(tabConfig.sortCriterion, tabConfig.sortAscending);
            tab->setHiddenColumns(tabConfig.hiddenColumns);
            restoredAny = true;
        }

        paneViewModel->setActiveTab(paneConfig.activeTabIndex);
    }

    if (restoredAny)
    {
        setLayout(config.layout);
        setFocusedPane(config.focusedPane);
    }

    return restoredAny;
}

void WorkspaceController::setLayout(SplitLayout layout)
{
    if (layout == m_layout)
    {
        return;
    }

    m_layout = layout;
    emit layoutChanged(layout);
    updateLiveWatchedTabs();
}

void WorkspaceController::setFocusedPane(WorkspacePaneId id)
{
    if (id == m_focusedPane)
    {
        return;
    }

    m_focusedPane = id;
    emit focusedPaneChanged(id);
}

void WorkspaceController::retargetFocusedTab()
{
    m_tagListViewModel->setActiveTab(focusedTab());
    m_filePreviewViewModel->setActiveTab(focusedTab());
    m_monitoringPanelViewModel->setFocusedTab(focusedTab());
    m_folderTreeModel->setActiveTab(focusedTab());
    emit focusedTabChanged(focusedTab());
}

void WorkspaceController::refreshTabsShowing(const std::filesystem::path& directory)
{
    for (WorkspacePaneViewModel* pane : m_panes)
    {
        for (int i = 0; i < pane->tabCount(); ++i)
        {
            TabViewModel* tab = pane->tabAt(i);
            if (tab->currentPath() == directory)
            {
                tab->refresh();
            }
        }
    }
}

void WorkspaceController::onPathRenamed(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    // Tree first, so the tabs' own re-listings below land on the already-renamed node rather than
    // replacing it (and its expanded subtree) with a fresh one.
    m_folderTreeModel->renameNode(source, destination);

    for (WorkspacePaneViewModel* pane : m_panes)
    {
        for (int i = 0; i < pane->tabCount(); ++i)
        {
            TabViewModel* tab = pane->tabAt(i);
            if (isPathWithinOrEqual(tab->currentPath(), source))
            {
                tab->replaceCurrentPath(rebasePath(tab->currentPath(), source, destination));
            }
        }
    }
}

void WorkspaceController::onPathRemoved(const std::filesystem::path& path)
{
    for (WorkspacePaneViewModel* pane : m_panes)
    {
        for (int i = 0; i < pane->tabCount(); ++i)
        {
            TabViewModel* tab = pane->tabAt(i);
            if (isPathWithinOrEqual(tab->currentPath(), path))
            {
                tab->replaceCurrentPath(path.parent_path());
            }
        }
    }
}

void WorkspaceController::updateLiveWatchedTabs()
{
    std::vector<TabViewModel*> visibleTabs;
    for (WorkspacePaneId id : WorkspaceLayoutTopology::visiblePanes(m_layout))
    {
        TabViewModel* tab = pane(id)->activeTab();
        if (tab != nullptr)
        {
            visibleTabs.push_back(tab);
        }
    }

    m_directoryWatchService->setVisibleTabs(visibleTabs);
}
