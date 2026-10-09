#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include "FileListModel.h"
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

    // Per-column hide/show override for THIS tab (Architecture.md §14.49), tri-state via
    // std::optional<bool>: nullopt means "never explicitly toggled in this tab" and defers to
    // Settings > General > Columns' "default visibility for new tab"
    // (ColumnVisibilitySettingsViewModel); Some(true)/Some(false) is this tab's own explicit
    // choice from its header right-click menu (FileBrowserView::showColumnMenu), which always
    // wins over that default once set. NameColumn's slot is always nullopt -- Name can never be
    // hidden (FileBrowserView::setColumnVisible already no-ops for it).
    std::array<std::optional<bool>, FileListModel::ColumnCount> hiddenColumns{};
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
