#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include "FileNavigationUseCase.h"
#include "SplitLayout.h"
#include "ViewMode.h"
#include "WorkspacePaneId.h"

// Plain-data snapshot of one tab's persisted state (Architecture.md §14.14). No Qt dependency,
// same posture as the enums it composes — the Qt/JSON boundary lives in src/adapters/config.
struct TabConfig
{
    std::filesystem::path path;
    ViewMode viewMode = ViewMode::Details;
    SortCriterion sortCriterion = SortCriterion::Name;
    bool sortAscending = true;
};

// Plain-data snapshot of one pane's ordered tabs plus which one was active.
struct PaneConfig
{
    std::vector<TabConfig> tabs;
    int activeTabIndex = 0;
};

// Plain-data snapshot of the whole workspace session: active SplitLayout, focused pane, and one
// PaneConfig per WorkspacePaneId (index via static_cast<int>(WorkspacePaneId)).
struct WorkspaceConfig
{
    SplitLayout layout = SplitLayout::Single;
    WorkspacePaneId focusedPane = WorkspacePaneId::PaneA;
    std::array<PaneConfig, 4> panes;
};
