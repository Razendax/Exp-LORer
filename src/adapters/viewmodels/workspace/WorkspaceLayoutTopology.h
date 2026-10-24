#pragma once

#include <optional>
#include <vector>

#include "SplitLayout.h"
#include "WorkspacePaneId.h"

// Pure mapping from a fixed SplitLayout to the WorkspacePaneId slots it makes visible. No Qt/UI
// dependency — plain logic, shared by WorkspaceLayoutWidget (to decide which panes to show/hide
// and how to group them into QSplitters) and WorkspaceController.
namespace WorkspaceLayoutTopology
{
    // Order matters: for TwoVertical/TwoHorizontal it is left-to-right/top-to-bottom reading
    // order; for FourGrid it is row-major {A, B, C, D} ({A,B} top row, {C,D} bottom row).
    std::vector<WorkspacePaneId> visiblePanes(SplitLayout layout);

    // Index math behind "Cycle focus to next/previous pane" (spec §3.3, Milestone 4): steps
    // `current` by `step` (+1 next, -1 previous) through visiblePanes(layout), wrapping modulo the
    // visible count. Returns nullopt -- a deliberate no-op for the caller -- when `current` isn't
    // among visiblePanes(layout) or fewer than 2 panes are visible (e.g. SplitLayout::Single has
    // nothing to cycle to).
    std::optional<WorkspacePaneId> cyclePane(SplitLayout layout, WorkspacePaneId current, int step);
}
