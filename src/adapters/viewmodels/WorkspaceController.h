#pragma once

#include <array>

#include <QObject>

#include "SplitLayout.h"
#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class WorkspacePaneViewModel;
class TabViewModel;

// Mediator owning all 4 WorkspacePaneViewModels (always instantiated, regardless of which are
// currently visible under the active SplitLayout) plus the active SplitLayout and the focused
// pane (Architecture.md §14). Lives in src/adapters, so it builds its panes directly in its
// constructor rather than going through CompositionRoot/src/app.
//
// Future hook point for retargeting the shared Tag/MediaPreview ViewModels: connect to
// focusedPaneChanged() and each pane's activeTabChanged() to retarget them at the focused pane's
// active tab. Not built in this increment.
class WorkspaceController : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceController(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    WorkspacePaneViewModel* pane(WorkspacePaneId id) const;
    SplitLayout layout() const noexcept { return m_layout; }
    WorkspacePaneId focusedPane() const noexcept { return m_focusedPane; }
    TabViewModel* focusedTab() const;

public slots:
    void setLayout(SplitLayout layout);
    void setFocusedPane(WorkspacePaneId id);

signals:
    void layoutChanged(SplitLayout layout);
    void focusedPaneChanged(WorkspacePaneId id);

private:
    std::array<WorkspacePaneViewModel*, 4> m_panes;
    SplitLayout m_layout = SplitLayout::Single;
    WorkspacePaneId m_focusedPane = WorkspacePaneId::PaneA;
};
