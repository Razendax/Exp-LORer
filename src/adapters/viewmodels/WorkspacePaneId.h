#pragma once

// Identity of one of the 4 fixed pane slots (Architecture.md §14). Deliberately geometry-agnostic
// (not TopLeft/TopRight) since the same slot plays a different visual role per SplitLayout.
enum class WorkspacePaneId
{
    PaneA,
    PaneB,
    PaneC,
    PaneD,
};
