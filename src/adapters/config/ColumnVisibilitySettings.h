#pragma once

#include <array>

#include "FileListModel.h"

// Default column visibility for newly created tabs (Settings > General > Columns), persisted via
// ColumnVisibilitySettingsStore. NameColumn's slot exists only for array-shape simplicity: Name is
// always visible, and neither the header's per-tab right-click menu nor this Settings page ever
// read/write it. Pure C++, no Qt -- same posture as NotificationSettings.
struct ColumnVisibilitySettings
{
    std::array<bool, FileListModel::ColumnCount> visible{};

    ColumnVisibilitySettings() { visible.fill(true); }
};
