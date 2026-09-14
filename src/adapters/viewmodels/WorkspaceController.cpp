#include "WorkspaceController.h"

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
{
    m_panes[indexOf(WorkspacePaneId::PaneA)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneA, this);
    m_panes[indexOf(WorkspacePaneId::PaneB)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneB, this);
    m_panes[indexOf(WorkspacePaneId::PaneC)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneC, this);
    m_panes[indexOf(WorkspacePaneId::PaneD)] = new WorkspacePaneViewModel(fileNavigationUseCase, WorkspacePaneId::PaneD, this);

    m_tagListViewModel = new TagListViewModel(tagManagementUseCase, fileNavigationUseCase, this);

    connect(this, &WorkspaceController::focusedPaneChanged, this, &WorkspaceController::retargetTagListViewModel);
    for (WorkspacePaneViewModel* pane : m_panes)
    {
        connect(pane, &WorkspacePaneViewModel::activeTabChanged, this, &WorkspaceController::retargetTagListViewModel);
    }

    retargetTagListViewModel();
}

WorkspacePaneViewModel* WorkspaceController::pane(WorkspacePaneId id) const
{
    return m_panes[static_cast<size_t>(indexOf(id))];
}

TabViewModel* WorkspaceController::focusedTab() const
{
    return pane(m_focusedPane)->activeTab();
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

void WorkspaceController::retargetTagListViewModel()
{
    m_tagListViewModel->setActiveTab(focusedTab());
}
