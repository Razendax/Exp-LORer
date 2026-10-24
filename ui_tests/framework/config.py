"""Fluent builder/writer for a test-owned workspace config file, so a test can pre-seed the
workspace layout/panes/tabs a fresh Exp-LORer instance boots into, instead of driving the UI into
that state by hand. Mirrors the JSON schema `AppConfigStore` reads/writes
(src/adapters/config/AppConfigStore.cpp, plain-data shape in
src/adapters/viewmodels/WorkspaceConfig.h) -- keep these in sync if that schema changes.

Writes directly to `workspaces/default.json` under `data_dir` (Architecture.md §14.60) -- the
default workspace's own config file, not the legacy top-level `config.json` (which only matters
for a one-time migration of a pre-existing *real* install's config, and ui_tests/ always starts
from an empty `data_dir`, so there's never anything there to migrate). `WorkspaceStore` self-heals
`workspaces/index.json` to the single "default" entry on first launch without touching an
already-written `workspaces/default.json`, so seeding it directly here still boots the app
straight into it.

Usage:

    config = ExpLorerConfig(data_dir)
    config.workspace.layout = SplitLayout.TWO_VERTICAL
    config.workspace.panes.a.add_tab(path="C:/some/folder")
    config.workspace.panes.b.add_tab()  # defaults to THIS_PC
    config.workspace.save()

    app = ExpLorerApp.start(data_dir=data_dir)  # boots straight into that layout

`config.workspace.save()` is a shortcut for `config.save()` -- the whole config file is written
either way, since `workspace` is currently the only section worth building fluently. Must be
written *before* `ExpLorerApp.start()` -- the app only reads this file at startup.
"""

from __future__ import annotations

import json
from enum import Enum
from pathlib import Path
from typing import List, Union

PathLike = Union[str, Path]


class SplitLayout(str, Enum):
    """Mirrors the C++ `SplitLayout` enum (src/adapters/viewmodels/SplitLayout.h)."""

    SINGLE = "Single"
    TWO_VERTICAL = "TwoVertical"
    TWO_HORIZONTAL = "TwoHorizontal"
    FOUR_GRID = "FourGrid"


class WorkspacePaneId(str, Enum):
    """Mirrors the C++ `WorkspacePaneId` enum (src/adapters/viewmodels/WorkspacePaneId.h)."""

    PANE_A = "PaneA"
    PANE_B = "PaneB"
    PANE_C = "PaneC"
    PANE_D = "PaneD"

# Matches FileListModel::ColumnCount (Name/Size/Type/DateModified/GitStatus/Tags/DateCreated/
# Percent/Fragments) -- src/adapters/viewmodels/filesystem/FileListModel.h.
_DETAILS_COLUMN_COUNT = 9

_CONFIG_VERSION = 1

# VirtualPaths::ThisPC's sentinel value (src/application/VirtualPaths.h) -- not the display
# label "This PC" shown in the UI. Use this as a tab's `path` to navigate it there.
THIS_PC = "this-pc:"


class TabConfig:
    """One tab: folder path + view/sort state. Mirrors `TabConfig` (WorkspaceConfig.h)."""

    def __init__(
        self,
        path: str = THIS_PC,
        view_mode: str = "Details",
        sort_criterion: str = "Name",
        sort_ascending: bool = True,
    ):
        self.path = path
        self.view_mode = view_mode
        self.sort_criterion = sort_criterion
        self.sort_ascending = sort_ascending

    def to_json(self) -> dict:
        return {
            "path": self.path,
            "viewMode": self.view_mode,
            "sortCriterion": self.sort_criterion,
            "sortAscending": self.sort_ascending,
        }


class PaneConfig:
    """One pane's ordered tabs -- `config.workspace.panes.a`. An empty tab list (the default)
    means "leave this pane at its own default" -- `WorkspaceController::restoreFromConfig` skips
    panes with no tabs rather than clearing them.
    """

    def __init__(self):
        self.tabs: List[TabConfig] = []
        self.active_tab_index: int = 0

    def add_tab(
        self,
        path: str = THIS_PC,
        view_mode: str = "Details",
        sort_criterion: str = "Name",
        sort_ascending: bool = True,
    ) -> "PaneConfig":
        """Appends a tab and returns self, so calls can be chained."""
        self.tabs.append(TabConfig(path, view_mode, sort_criterion, sort_ascending))
        return self

    def to_json(self) -> dict:
        return {
            "activeTabIndex": self.active_tab_index,
            "tabs": [tab.to_json() for tab in self.tabs],
        }


class WorkspacePanesConfig:
    """`config.workspace.panes.a/b/c/d` -- one `PaneConfig` per `WorkspacePaneId`."""

    def __init__(self):
        self.a = PaneConfig()
        self.b = PaneConfig()
        self.c = PaneConfig()
        self.d = PaneConfig()

    def to_json(self) -> dict:
        return {
            "PaneA": self.a.to_json(),
            "PaneB": self.b.to_json(),
            "PaneC": self.c.to_json(),
            "PaneD": self.d.to_json(),
        }


class WorkspaceConfig:
    """`config.workspace` -- layout/focus/panes, mirroring `WorkspaceConfig` (WorkspaceConfig.h).
    `.save()` is a passthrough to the owning `ExpLorerConfig.save()`.
    """

    def __init__(self, owner: "ExpLorerConfig"):
        self._owner = owner
        self.layout: SplitLayout = SplitLayout.SINGLE
        self.focused_pane: WorkspacePaneId = WorkspacePaneId.PANE_A
        self.panes = WorkspacePanesConfig()

    def to_json(self) -> dict:
        return {
            "layout": SplitLayout(self.layout).value,
            "focusedPane": WorkspacePaneId(self.focused_pane).value,
            "panes": self.panes.to_json(),
        }

    def save(self) -> Path:
        """Writes the whole config.json (not just the workspace section) -- see `ExpLorerConfig.save`."""
        return self._owner.save()


class ExpLorerConfig:
    """Root builder for a test-owned default-workspace config file, written under `data_dir` (the
    same directory passed to `ExpLorerApp.start(data_dir=...)`/`--data-dir`).
    """

    def __init__(self, data_dir: PathLike):
        self.data_dir = Path(data_dir)
        self.workspace = WorkspaceConfig(self)
        self.window_geometry_base64: str = ""
        self.details_column_widths: List[int] = [0] * _DETAILS_COLUMN_COUNT

    @property
    def path(self) -> Path:
        """Full path to the config file this builder writes -- `WorkspaceStore::configFilePath`
        for the default workspace id (src/adapters/config/WorkspaceStore.cpp).
        """
        return self.data_dir / "workspaces" / "default.json"

    def to_json(self) -> dict:
        return {
            "version": _CONFIG_VERSION,
            "windowGeometry": self.window_geometry_base64,
            "workspace": self.workspace.to_json(),
            "detailsColumnWidths": self.details_column_widths,
        }

    def save(self) -> Path:
        """Writes the config file into `data_dir/workspaces`, creating the directory if needed.
        Returns the written path.
        """
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.to_json(), indent=2), encoding="utf-8")
        return self.path
