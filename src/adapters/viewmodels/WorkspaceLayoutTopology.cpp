#include "WorkspaceLayoutTopology.h"

std::vector<WorkspacePaneId> WorkspaceLayoutTopology::visiblePanes(SplitLayout layout)
{
    switch (layout)
    {
        case SplitLayout::Single:
            return { WorkspacePaneId::PaneA };
        case SplitLayout::TwoVertical:
        case SplitLayout::TwoHorizontal:
            return { WorkspacePaneId::PaneA, WorkspacePaneId::PaneB };
        case SplitLayout::FourGrid:
            return { WorkspacePaneId::PaneA, WorkspacePaneId::PaneB, WorkspacePaneId::PaneC, WorkspacePaneId::PaneD };
    }
    return { WorkspacePaneId::PaneA };
}
