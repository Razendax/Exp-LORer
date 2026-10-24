#include "WorkspaceLayoutTopology.h"

#include <algorithm>
#include <cstddef>

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

std::optional<WorkspacePaneId> WorkspaceLayoutTopology::cyclePane(SplitLayout layout, WorkspacePaneId current, int step)
{
    const std::vector<WorkspacePaneId> panes = visiblePanes(layout);
    if (panes.size() < 2)
    {
        return std::nullopt;
    }

    const auto it = std::find(panes.begin(), panes.end(), current);
    if (it == panes.end())
    {
        return std::nullopt;
    }

    const int count = static_cast<int>(panes.size());
    const int index = static_cast<int>(std::distance(panes.begin(), it));
    const int nextIndex = ((index + step) % count + count) % count;
    return panes[static_cast<std::size_t>(nextIndex)];
}
