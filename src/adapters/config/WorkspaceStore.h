#pragma once

#include <filesystem>
#include <vector>

#include <QString>

#include "WorkspaceEntry.h"

// Owns workspaces/index.json (the {id, name} registry) under appDataDir, and builds -- but never
// loads/saves -- each workspace's own AppConfig file path (Architecture.md §14.60). Callers point a
// plain AppConfigStore at configFilePath(id) themselves; this class never touches AppConfig/
// AppConfigStore directly, so AppConfigStore itself needs zero changes for workspaces to exist.
//
// Migration: the first time workspaces/index.json doesn't exist yet, a pre-existing legacy
// appDataDir/config.json (if any) is copied into workspaces/default.json (id "default", name
// "Default") and the index is created with that single entry. If the index is ever empty (fresh
// install with no legacy config.json either, or every workspace having been deleted), it self-heals
// to that same single "default"/"Default" entry -- same tolerant-fallback posture AppConfigStore::
// load() already has for a missing file. Both the migration and the self-heal run lazily, inside
// listWorkspaces(), rather than in the constructor, so CompositionRoot can trigger them simply by
// constructing WorkspaceStore before AppConfigStore and calling listWorkspaces() (or any other
// method here) before reading the resolved workspace's own config file.
//
// Built on JsonFileStore (same tolerant-load/atomic-save shape as every other store in this
// directory). Every method here is const: like AppConfigStore/FavoritesStore, this class holds no
// mutable in-memory state of its own -- index.json on disk is the only state, read fresh and
// written atomically on every call.
class WorkspaceStore
{
public:
    explicit WorkspaceStore(std::filesystem::path appDataDir);

    // Runs the migration/self-heal described above (if needed) and returns the current registry, in
    // on-disk order.
    std::vector<WorkspaceEntry> listWorkspaces() const;

    // Pure path build, no I/O: appDataDir/workspaces/<id>.json.
    std::filesystem::path configFilePath(const QString& id) const;

    // Appends a new entry (a fresh QUuid) with `name` to the index and returns its id. Does not
    // create <id>.json -- the caller (e.g. MainWindow's "Save as Workspace...") writes that itself
    // via AppConfigStore::save, pointed at configFilePath(the returned id).
    QString createWorkspace(const QString& name);

    // Renames the entry for `id` in the index. Never touches <id>.json, so it's always safe even
    // while that workspace is open in another running process (see class comment above).
    bool renameWorkspace(const QString& id, const QString& newName);

    // Removes the entry for `id` from the index and deletes <id>.json. Callers are responsible for
    // checking the workspace isn't currently open elsewhere first (WorkspaceInstanceGuard::isOwned)
    // -- this method itself has no way to know.
    bool deleteWorkspace(const QString& id);

private:
    std::vector<WorkspaceEntry> loadIndex() const;
    bool saveIndex(const std::vector<WorkspaceEntry>& entries) const;

    // Migration + empty-index self-heal, run once per call via listWorkspaces()/createWorkspace()/
    // etc. Idempotent: a no-op once workspaces/index.json already exists and is non-empty.
    std::vector<WorkspaceEntry> ensureMigratedAndNonEmpty() const;

    std::filesystem::path m_appDataDir;
    std::filesystem::path m_workspacesDir;
    std::filesystem::path m_indexFilePath;
    std::filesystem::path m_legacyConfigFilePath;
};
