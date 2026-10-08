#pragma once

// Git working-tree status for a single file/folder (Architecture.md §14.x, "Git status badges").
// Values are listed highest-priority first -- the order used when a path has more than one git2
// status bit set at once (e.g. staged AND unstaged changes); the provider collapses each path down
// to a single value by walking this list top to bottom and taking the first match. Conflicted is
// also the fold-in bucket for the rare typechange/conflict cases the UI has no dedicated badge for.
enum class GitFileStatus
{
    Conflicted,
    Untracked,
    Ignored,
    DeletedUnstaged,
    ModifiedUnstaged,
    DeletedStaged,
    AddedStaged,
    ModifiedStaged,
};
