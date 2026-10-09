#pragma once

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
}
