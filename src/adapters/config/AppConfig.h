#pragma once

#include <array>

#include <QByteArray>

#include "FileListModel.h"
#include "WorkspaceConfig.h"

// One SidePanelWidget's persisted state (Architecture.md §14.25/§14.41 — previously unpersisted).
// activeTabIndex/width are only meaningful while expanded; width 0 is the "unset" sentinel (same
// convention as detailsColumnWidths) -- SidePanelWidget falls back to its own computed default.
struct SidePanelState
{
    bool expanded = false;
    int activeTabIndex = -1;
    int width = 0; 
};

// Qt-dependent persistence-boundary type (Architecture.md §14.14) — unlike WorkspaceConfig, this
// struct is allowed to reference Qt directly since src/adapters/config is the Qt/JSON boundary,
// the same posture SQLiteTagRepository has at its own boundary.
struct AppConfig
{
    QByteArray windowGeometry;
    WorkspaceConfig workspace;

    // Left panel (Tree/Search) and right panel (Tags/Preview/Monitoring) expand/tab/width state
    // (Architecture.md §14.14, §14.25/§14.41) -- same save-on-close/restore-on-launch posture as
    // every other field here.
    SidePanelState leftPanel;
    SidePanelState rightPanel;

    // Details-view column widths (indexed by FileListModel::Column), shared across every tab/pane
    // rather than captured per-tab like TabConfig's ViewMode/SortCriterion — a column layout is a
    // property of the Name/Size/Type/Date-modified columns themselves, not of any one folder. 0
    // means "unset": FileBrowserView leaves that column at QHeaderView's own default width instead
    // of forcing it, so a fresh install still gets Qt's normal initial sizing.
    std::array<int, FileListModel::ColumnCount> detailsColumnWidths{};

    // Details-view column left-to-right order: entry [visual position] = FileListModel::Column
    // logical index shown there, same cross-tab/pane sharing rationale as detailsColumnWidths
    // above. Zero-initialized (all entries 0) is the "unset" sentinel -- that's not a valid
    // permutation of 0..ColumnCount-1, so FileBrowserView::setColumnOrder() detects it and leaves
    // QHeaderView's own default left-to-right order alone on a fresh install.
    std::array<int, FileListModel::ColumnCount> detailsColumnOrder{};
};
