#pragma once

#include <QByteArray>

#include "WorkspaceConfig.h"

// Qt-dependent persistence-boundary type (Architecture.md §14.14) — unlike WorkspaceConfig, this
// struct is allowed to reference Qt directly since src/adapters/config is the Qt/JSON boundary,
// the same posture SQLiteTagRepository/ContextMenuIcon already have at their own boundaries.
struct AppConfig
{
    QByteArray windowGeometry;
    WorkspaceConfig workspace;
};
