#pragma once

#include <filesystem>

#include <QString>

// The two values a window needs to re-launch itself against a different saved workspace (File >
// Workspaces, Architecture.md §14.60) via QProcess::startDetached -- bundled together rather than
// growing MainWindow's already-long constructor parameter list with two more raw path/string
// params. appDataDir/workspaceId are exactly the "--data-dir"/"--workspace" values this process
// itself was launched with (or their defaults).
struct WorkspaceLaunchContext
{
    std::filesystem::path appDataDir;
    QString workspaceId;
};
