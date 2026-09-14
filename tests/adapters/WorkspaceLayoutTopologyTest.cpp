#include <gtest/gtest.h>

#include "WorkspaceLayoutTopology.h"

TEST(WorkspaceLayoutTopology, SingleShowsOnlyPaneA)
{
    const auto panes = WorkspaceLayoutTopology::visiblePanes(SplitLayout::Single);

    EXPECT_EQ(panes, std::vector<WorkspacePaneId>({ WorkspacePaneId::PaneA }));
}

TEST(WorkspaceLayoutTopology, TwoVerticalShowsPaneAAndPaneB)
{
    const auto panes = WorkspaceLayoutTopology::visiblePanes(SplitLayout::TwoVertical);

    EXPECT_EQ(panes, std::vector<WorkspacePaneId>({ WorkspacePaneId::PaneA, WorkspacePaneId::PaneB }));
}

TEST(WorkspaceLayoutTopology, TwoHorizontalShowsPaneAAndPaneB)
{
    const auto panes = WorkspaceLayoutTopology::visiblePanes(SplitLayout::TwoHorizontal);

    EXPECT_EQ(panes, std::vector<WorkspacePaneId>({ WorkspacePaneId::PaneA, WorkspacePaneId::PaneB }));
}

TEST(WorkspaceLayoutTopology, FourGridShowsAllFourPanesInRowMajorOrder)
{
    const auto panes = WorkspaceLayoutTopology::visiblePanes(SplitLayout::FourGrid);

    EXPECT_EQ(panes, std::vector<WorkspacePaneId>({
        WorkspacePaneId::PaneA,
        WorkspacePaneId::PaneB,
        WorkspacePaneId::PaneC,
        WorkspacePaneId::PaneD,
    }));
}
