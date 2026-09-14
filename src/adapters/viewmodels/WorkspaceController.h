#pragma once

#include <array>

#include <QObject>

#include "SplitLayout.h"
#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class WorkspacePaneViewModel;
class TabViewModel;
class TagListViewModel;

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

public slots:
    void setLayout(SplitLayout layout);
    void setFocusedPane(WorkspacePaneId id);

signals:
    void layoutChanged(SplitLayout layout);
    void focusedPaneChanged(WorkspacePaneId id);

private:
    void retargetTagListViewModel();

    std::array<WorkspacePaneViewModel*, 4> m_panes;
    SplitLayout m_layout = SplitLayout::Single;
    WorkspacePaneId m_focusedPane = WorkspacePaneId::PaneA;
    TagListViewModel* m_tagListViewModel = nullptr;
};
