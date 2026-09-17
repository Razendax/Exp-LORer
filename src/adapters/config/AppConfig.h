#pragma once

#include <array>

#include <QByteArray>

#include "FileListModel.h"
#include "WorkspaceConfig.h"

// Qt-dependent persistence-boundary type (Architecture.md §14.14) — unlike WorkspaceConfig, this
// struct is allowed to reference Qt directly since src/adapters/config is the Qt/JSON boundary,
// the same posture SQLiteTagRepository/ContextMenuIcon already have at their own boundaries.
struct AppConfig
{
    QByteArray windowGeometry;
    WorkspaceConfig workspace;

    // Details-view column widths (indexed by FileListModel::Column), shared across every tab/pane
    // rather than captured per-tab like TabConfig's ViewMode/SortCriterion — a column layout is a
    // property of the Name/Size/Type/Date-modified columns themselves, not of any one folder. 0
    // means "unset": FileBrowserView leaves that column at QHeaderView's own default width instead
    // of forcing it, so a fresh install still gets Qt's normal initial sizing.
    std::array<int, FileListModel::ColumnCount> detailsColumnWidths{};
};
