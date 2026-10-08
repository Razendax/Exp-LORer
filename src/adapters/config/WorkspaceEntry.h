#pragma once

#include <QString>

// One row of workspaces/index.json (Architecture.md §14.60) -- a workspace's stable id (a QUuid's
// text form, or the literal "default" for the migrated/self-healed single workspace) and its
// user-facing display name. The actual session state for an id lives separately, in
// WorkspaceStore::configFilePath(id), read/written by a plain AppConfigStore.
struct WorkspaceEntry
{
    QString id;
    QString name;
};
