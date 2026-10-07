#pragma once

#include <vector>

#include <QString>

#include "EnvironmentVariable.h"

// Applies the custom per-process environment variable list to the real process environment
// (Architecture.md §14.57) via SetEnvironmentVariableW -- a free function, not Qt-widget-dependent,
// so it stays independently testable. Windows-only for now, same posture as §14.54's
// StandardFileSystemRepository::expandEnvironmentVariables (Specification.md §1.2); the non-Windows
// branch is a no-op.
namespace EnvironmentVariableApplier
{
    // Clears every name in `previouslyAppliedNames` first (so a removed/renamed variable doesn't
    // linger), then sets each of `variables`' current name/value.
    void apply(const std::vector<EnvironmentVariable>& variables, const std::vector<QString>& previouslyAppliedNames);

    // Snapshot of every variable already in this process's environment (GetEnvironmentStringsW),
    // used to seed EnvironmentVariablesStore the first time it's empty (Architecture.md §14.57) so
    // the dialog opens showing the variables that already exist (PATH, TEMP, ...) instead of blank.
    // A multi-value variable (PATH-style) is split on ";" the same way joinEnvironmentVariableValues
    // joins values back, so round-tripping through the dialog without edits reproduces the original
    // string exactly. Windows' hidden per-drive pseudo-variables (name starting with "=", e.g.
    // "=C:=C:\\Users\\...") are skipped. Windows-only; empty on other platforms.
    std::vector<EnvironmentVariable> currentProcessEnvironmentSnapshot();
}
