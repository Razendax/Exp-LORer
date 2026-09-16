#include "WorkspaceController.h"

#include "FileOperationsController.h"
#include "TabViewModel.h"
#include "TagListViewModel.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    int indexOf(WorkspacePaneId id)
    {
        return static_cast<int>(id);
    }
}

WorkspaceController::WorkspaceController(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                                           QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
{
    m_panes[indexOf(WorkspacePaneId::PaneA)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneA, this);
    m_panes[indexOf(WorkspacePaneId::PaneB)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneB, this);
    m_panes[indexOf(WorkspacePaneId::PaneC)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneC, this);
    m_panes[indexOf(WorkspacePaneId::PaneD)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneD, this);

    m_tagListViewModel = new TagListViewModel(tagManagementUseCase, fileNavigationUseCase, this);
    m_fileOperationsController = new FileOperationsController(m_fileNavigationUseCase, this);

    connect(this, &WorkspaceController::focusedPaneChanged, this, &WorkspaceController::retargetFocusedTab);
    for (WorkspacePaneViewModel* pane : m_panes)
    {
        connect(pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspaceController::retargetFocusedTab);
    }

    connect(m_fileOperationsController, &FileOperationsController::directoryContentsMayHaveChanged, this,
            &WorkspaceController::refreshTabsShowing);

    retargetFocusedTab();
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
            paneConfig.tabs.push_back(TabConfig{ tab->currentPath(), tab->viewMode(), tab->sortCriterion(), tab->sortAscending() });
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
