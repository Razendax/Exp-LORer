"""Coverage for Details-view column customization (Customizable Details-View Columns feature,
src/ui/widgets/FileBrowserView.{h,cpp}'s showColumnMenu/setColumnOrder,
src/adapters/config/ColumnVisibilitySettingsStore.cpp) -- per-tab visibility via the header's
right-click menu, a new tab's default column visibility (`column_visibility.json`), and column
reordering (drag) plus its persistence across a restart. See `TestDefaultColumnsForNewTabs`'s own
docstring for why the default-visibility coverage goes through the persisted settings file rather
than the Settings dialog's own checkboxes.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path
from typing import List, Optional

from framework import (
    TOGGLEABLE_COLUMN_TITLES,
    ExpLorerApp,
    ExpLorerConfig,
    ExpLorerFreshAppTestCase,
    ExpLorerRestartableTestCase,
    drag_header,
    read_header_titles,
    toggle_column_visibility,
)
from framework.wait import wait_until


def _make_one_file(target: Path) -> None:
    target.mkdir()
    (target / "f.txt").write_text("hello")


def _changed_titles(tree, before: List[str]) -> Optional[List[str]]:
    current = read_header_titles(tree)
    return current if current != before else None


class TestColumnVisibilityMenu(ExpLorerFreshAppTestCase):
    """Fresh instance per test method -- toggling a column's visibility sticks on the shared tab
    (Architecture.md: "per-tab, not session state"), which would otherwise leak from one test
    method into the next.
    """

    def test_every_toggleable_column_is_visible_by_default(self, tmp_path: Path) -> None:
        target = tmp_path / "defaults"
        _make_one_file(target)
        self.app.navigation.navigate_to(str(target))

        tree = self.app.panes.a.file_tree_view
        titles = wait_until(lambda: read_header_titles(tree) or None)
        assert titles == ["Name", *TOGGLEABLE_COLUMN_TITLES]

    def test_unchecking_a_column_in_the_header_menu_hides_it(self, tmp_path: Path) -> None:
        target = tmp_path / "hide"
        _make_one_file(target)
        self.app.navigation.navigate_to(str(target))

        tree = self.app.panes.a.file_tree_view
        wait_until(lambda: read_header_titles(tree) or None)

        toggle_column_visibility(tree, "Tags")

        wait_until(lambda: "Tags" not in read_header_titles(tree))
        titles = read_header_titles(tree)
        assert titles == ["Name"] + [title for title in TOGGLEABLE_COLUMN_TITLES if title != "Tags"]

    def test_rechecking_a_hidden_column_shows_it_again(self, tmp_path: Path) -> None:
        target = tmp_path / "reshow"
        _make_one_file(target)
        self.app.navigation.navigate_to(str(target))

        tree = self.app.panes.a.file_tree_view
        wait_until(lambda: read_header_titles(tree) or None)

        toggle_column_visibility(tree, "Percent")
        wait_until(lambda: "Percent" not in read_header_titles(tree))

        toggle_column_visibility(tree, "Percent")
        wait_until(lambda: "Percent" in read_header_titles(tree))
        assert read_header_titles(tree) == ["Name", *TOGGLEABLE_COLUMN_TITLES]


class TestDefaultColumnsForNewTabs:
    """Not an `ExpLorerTestCase`/`ExpLorerFreshAppTestCase` subclass -- this test seeds
    `column_visibility.json` (`ColumnVisibilitySettingsStore`'s own file, alongside config.json
    under the data directory) *before* the app starts, which needs the same manual `start_app`
    control `TestColumnOrderPersistence` uses (see its own docstring for why that doesn't need a
    dedicated TestCase base class).

    Driving this from the Settings dialog's own "Default visibility for new tab" checkboxes
    instead (`SettingsDialog::buildColumnsPage`) was tried and dropped: those are real
    `QTableWidget::setCellWidget()`-embedded `QCheckBox`es, and neither a mouse click at their
    on-screen center (confirmed against a captured screenshot), a double-click, keyboard focus +
    Space, nor UI Automation's Invoke pattern actually toggled one, against a real build -- so this
    test instead verifies the underlying behavior (a tab honoring a persisted default) directly
    against the same file the Settings page itself reads/writes.
    """

    def test_new_tab_omits_a_column_whose_default_visibility_is_off(
        self, start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
    ) -> None:
        target = tmp_path / "default_visibility"
        _make_one_file(target)

        data_dir = tmp_path / "data"
        data_dir.mkdir(parents=True)
        (data_dir / "column_visibility.json").write_text(
            json.dumps({"version": 1, "columns": {"Percent": False}}),
            encoding="utf-8",
        )

        config = ExpLorerConfig(data_dir)
        config.workspace.panes.a.add_tab(path=str(target))
        config.workspace.save()

        app = start_app(data_dir)
        tree = app.panes.a.file_tree_view
        titles = wait_until(lambda: read_header_titles(tree) or None)

        # Every other column keeps its own normal (checked) default -- only Percent was seeded off.
        assert titles == ["Name"] + [title for title in TOGGLEABLE_COLUMN_TITLES if title != "Percent"]


class TestColumnVisibilityPersistsPerTabAcrossRestart(ExpLorerRestartableTestCase):
    """Architecture.md §14.49: the header-menu toggle is per-*tab*, not a session-wide setting, so
    this is the key proof that independence actually survives a restart -- not just that some
    column's state round-trips in isolation (that's covered by `WorkspaceControllerTest`/
    `AppConfigStoreTest` at the adapter level; this suite covers the restore-on-launch wiring
    itself, same posture as `ExpLorerRestartableTestCase`'s other users).
    """

    def test_hiding_a_column_in_one_tab_leaves_another_tab_and_a_post_restart_new_tab_on_default(self) -> None:
        tabs = self.app.panes.a.tabs
        tree = self.app.panes.a.file_tree_view

        wait_until(lambda: read_header_titles(tree) or None)

        # Tab B, opened before the toggle below -- it must keep following the Settings-dialog
        # default (every column visible) rather than inheriting tab A's later choice.
        self.app.navigation.new_tab.click()
        wait_until(lambda: tabs.titles() == ["This PC", "This PC"])

        tabs.select(0)
        wait_until(lambda: read_header_titles(tree) or None)
        toggle_column_visibility(tree, "Tags")
        wait_until(lambda: "Tags" not in read_header_titles(tree))

        tabs.select(1)
        wait_until(lambda: "Tags" in read_header_titles(tree))

        self.restart()

        tabs = self.app.panes.a.tabs
        tree = self.app.panes.a.file_tree_view
        wait_until(lambda: tabs.titles() == ["This PC", "This PC"])

        tabs.select(0)
        wait_until(lambda: read_header_titles(tree) or None)
        assert "Tags" not in read_header_titles(tree)  # tab A's own explicit choice survived

        tabs.select(1)
        wait_until(lambda: read_header_titles(tree) or None)
        assert "Tags" in read_header_titles(tree)  # tab B never touched it -- still on default

        # Tab C, created fresh only after the restart -- it has no restored record at all, so it
        # must also follow the Settings-dialog default, not tab A's restored choice.
        self.app.navigation.new_tab.click()
        wait_until(lambda: tabs.titles() == ["This PC", "This PC", "This PC"])
        tabs.select(2)
        wait_until(lambda: read_header_titles(tree) or None)
        assert "Tags" in read_header_titles(tree)


class TestColumnReordering(ExpLorerFreshAppTestCase):
    def test_dragging_a_header_reorders_the_visible_columns(self, tmp_path: Path) -> None:
        target = tmp_path / "reorder"
        _make_one_file(target)
        self.app.navigation.navigate_to(str(target))

        tree = self.app.panes.a.file_tree_view
        before = wait_until(lambda: read_header_titles(tree) or None)
        assert before.index("Size") < before.index("Type")  # default (identity) order

        drag_header(tree, "Size", "Type")

        after = wait_until(lambda: _changed_titles(tree, before))
        assert after.index("Size") > after.index("Type")  # order swapped


class TestColumnOrderPersistence:
    """Not an `ExpLorerTestCase`/`ExpLorerFreshAppTestCase` subclass -- this test needs to seed a
    config.json *before* the app starts and close/restart the same instance mid-test to check what
    got saved, which the `start_app` factory fixture already gives directly (the same fixture
    `test_config_save_on_close.py` uses for exactly that reason) -- there's no shared setup/
    teardown behavior here for a dedicated TestCase base class to usefully factor out.
    """

    def test_reordered_columns_persist_after_restart(
        self, start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
    ) -> None:
        target = tmp_path / "reorder_persist"
        _make_one_file(target)

        data_dir = tmp_path / "data"
        config = ExpLorerConfig(data_dir)
        config.workspace.panes.a.add_tab(path=str(target))
        config.workspace.save()

        first = start_app(data_dir)
        tree = first.panes.a.file_tree_view
        before = wait_until(lambda: read_header_titles(tree) or None)

        drag_header(tree, "Size", "Type")

        after_drag = wait_until(lambda: _changed_titles(tree, before))
        assert after_drag.index("Size") > after_drag.index("Type")

        first.close()

        second = start_app(data_dir)
        second_tree = second.panes.a.file_tree_view
        after_restart = wait_until(lambda: read_header_titles(second_tree) or None)
        assert after_restart.index("Size") > after_restart.index("Type")
