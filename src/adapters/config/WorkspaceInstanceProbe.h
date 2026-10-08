#pragma once

#include <filesystem>

#include <QString>

// Pure channel-name derivation plus a non-destructive "is anyone listening" probe for the
// per-workspace single-instance IPC channel (Architecture.md §14.60). Lives here, in
// src/adapters/config (which src/ui/widgets already links), rather than alongside the full
// mutex-owning WorkspaceInstanceGuard in src/app -- that keeps the dependency pointing the right
// way: WorkspaceManagerDialog (ui) needs to ask "is this workspace open somewhere right now?"
// without ui depending on src/app, which is the outermost composition layer (Architecture.md,
// "src/app ... the only place concrete Qt/SQLite/FFmpeg types meet the use cases"). The actual
// process-owning mutex + activation QLocalServer stay in WorkspaceInstanceGuard, which reuses
// deriveChannelName() here so both sides agree on the same name without duplicating the hash logic.
namespace WorkspaceInstanceProbe
{
    QString deriveChannelName(const std::filesystem::path& appDataDir, const QString& workspaceId);

    // True if some process currently owns the guard name for (appDataDir, workspaceId) -- i.e. that
    // workspace is open right now, in this window or another. Windows-only; always false elsewhere
    // (consistent with WorkspaceInstanceGuard itself not being implemented off Windows yet).
    bool isOwned(const std::filesystem::path& appDataDir, const QString& workspaceId);
}
