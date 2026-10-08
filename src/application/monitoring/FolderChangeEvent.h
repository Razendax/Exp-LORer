#pragma once

#include <filesystem>

// One recursive-monitoring change (Architecture.md §14.x, "Monitoring panel"). Mirrors
// GitFileStatus.h/DriveSpaceInfo.h -- plain data crossing the Application boundary.
enum class FolderChangeKind
{
    Added,
    Removed,
    Modified,
};

struct FolderChangeEvent
{
    std::filesystem::path relativePath; // relative to the monitored root, not the OS-reported path
    FolderChangeKind kind = FolderChangeKind::Modified;
    bool isDirectory = false; // best-effort for Removed -- see StandardFolderMonitor
};
