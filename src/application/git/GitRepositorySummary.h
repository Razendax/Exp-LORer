#pragma once

#include <filesystem>
#include <optional>
#include <string>

// Plain data crossing the Application boundary -- one directory's repository-wide branch/change
// summary (Architecture.md §14.x, "Git branch indicator and checkout"). repositoryRoot is nullopt
// when the queried directory isn't inside any git repository, in which case the rest of the fields
// are left at their defaults (same "tolerate by blanking" posture as GitDirectoryStatus).
struct GitRepositorySummary
{
    std::optional<std::filesystem::path> repositoryRoot;
    std::string headDisplayName;   // branch short name, or short commit id if detached, or the
                                    // would-be branch name pre-first-commit (unborn HEAD)
    bool isDetached = false;
    int changedFileCount = 0;      // repo-wide, non-Ignored entries
};
