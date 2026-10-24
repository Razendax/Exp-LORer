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

TEST(WorkspaceLayoutTopologyCyclePane, SingleLayoutHasNothingToCycleTo)
{
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::Single, WorkspacePaneId::PaneA, 1), std::nullopt);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::Single, WorkspacePaneId::PaneA, -1), std::nullopt);
}

TEST(WorkspaceLayoutTopologyCyclePane, TwoPaneLayoutStepsForwardAndWraps)
{
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::TwoVertical, WorkspacePaneId::PaneA, 1), WorkspacePaneId::PaneB);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::TwoVertical, WorkspacePaneId::PaneB, 1), WorkspacePaneId::PaneA);
}

TEST(WorkspaceLayoutTopologyCyclePane, TwoPaneLayoutStepsBackwardAndWraps)
{
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::TwoHorizontal, WorkspacePaneId::PaneA, -1), WorkspacePaneId::PaneB);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::TwoHorizontal, WorkspacePaneId::PaneB, -1), WorkspacePaneId::PaneA);
}

TEST(WorkspaceLayoutTopologyCyclePane, FourGridStepsForwardThroughAllFourAndWraps)
{
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneA, 1), WorkspacePaneId::PaneB);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneB, 1), WorkspacePaneId::PaneC);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneC, 1), WorkspacePaneId::PaneD);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneD, 1), WorkspacePaneId::PaneA);
}

TEST(WorkspaceLayoutTopologyCyclePane, FourGridStepsBackwardThroughAllFourAndWraps)
{
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneA, -1), WorkspacePaneId::PaneD);
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::FourGrid, WorkspacePaneId::PaneD, -1), WorkspacePaneId::PaneC);
}

TEST(WorkspaceLayoutTopologyCyclePane, CurrentPaneNotVisibleUnderLayoutIsANoOp)
{
    // PaneC/PaneD aren't in visiblePanes(TwoVertical) -- a stale/mismatched focusedPane should
    // never crash or silently wrap to an arbitrary index.
    EXPECT_EQ(WorkspaceLayoutTopology::cyclePane(SplitLayout::TwoVertical, WorkspacePaneId::PaneC, 1), std::nullopt);
}
