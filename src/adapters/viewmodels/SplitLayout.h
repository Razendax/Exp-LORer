#pragma once

// Fixed set of window-split arrangements (Architecture.md §14). No recursive/arbitrary nesting:
// each layout maps to a fixed set of visible WorkspacePaneId slots via WorkspaceLayoutTopology.
enum class SplitLayout
{
    Single,
    TwoVertical,
    TwoHorizontal,
    FourGrid,
};
