"""Coverage for the Details view refreshing live when the current folder's contents change on
disk -- driven by DirectoryWatchService's debounced watch (src/adapters/viewmodels/
DirectoryWatchService.h, ~400ms debounce), not a manual refresh click.

Uses `ExpLorerTestCase` (one shared Exp-LORer instance for the class) rather than the `app`
fixture -- each test method navigates to its own fresh `tmp_path` target before asserting, so
there's no cross-test state to isolate against.
"""

from __future__ import annotations

import time
from pathlib import Path

from framework import ExpLorerFreshAppTestCase, ExpLorerTestCase
from framework.details_view import read_details_rows
from framework.wait import wait_until


class TestLiveUpdate(ExpLorerTestCase):
    def test_new_file_appears_without_manual_refresh(self, tmp_path: Path) -> None:
        target = tmp_path / "watched"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        assert read_details_rows(self.app.panes.a.file_tree_view) == []

        (target / "new.txt").write_text("hello")

        rows = wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)
        assert [row.name for row in rows] == ["new.txt"]

    def test_deleted_file_disappears_without_manual_refresh(self, tmp_path: Path) -> None:
        target = tmp_path / "watched"
        target.mkdir()
        doomed = target / "doomed.txt"
        doomed.write_text("bye")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)

        doomed.unlink()

        wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) == [])
        assert read_details_rows(self.app.panes.a.file_tree_view) == []

    def test_modified_file_size_updates_without_manual_refresh(self, tmp_path: Path) -> None:
        target = tmp_path / "watched"
        target.mkdir()
        growing = target / "growing.txt"
        growing.write_text("a")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)

        growing.write_text("a" * 10)

        def grown_to_ten_bytes():
            rows = read_details_rows(self.app.panes.a.file_tree_view)
            return rows and rows[0].size == "10 B"

        wait_until(grown_to_ten_bytes)

    def test_new_subfolder_appears_without_manual_refresh(self, tmp_path: Path) -> None:
        target = tmp_path / "watched"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        assert read_details_rows(self.app.panes.a.file_tree_view) == []

        (target / "new_subfolder").mkdir()

        rows = wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)
        assert rows[0].name == "new_subfolder"
        assert rows[0].type == "File folder"

    def test_change_in_a_subfolder_does_not_refresh_the_watched_folders_own_listing(self, tmp_path: Path) -> None:
        """DirectoryWatchService watches each visible tab's folder non-recursively (Architecture.md
        §14.33) -- a change several levels down must not, by itself, be what refreshes this
        folder's own listing (only its immediate children are watched).
        """
        target = tmp_path / "watched"
        sub = target / "sub"
        sub.mkdir(parents=True)
        self.app.navigation.navigate_to(str(target))
        rows = wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)
        assert [row.name for row in rows] == ["sub"]

        (sub / "deep.txt").write_text("x")

        # No manual refresh: give the (non-existent, for this change) watch callback a window to
        # have fired if it incorrectly were recursive, then confirm the listing is still just the
        # unchanged "sub" row -- a manual refresh afterward must still show the same, proving this
        # isn't a false negative from reading too early.
        time.sleep(1.5)
        assert [row.name for row in read_details_rows(self.app.panes.a.file_tree_view)] == ["sub"]

        self.app.navigation.refresh.click()
        wait_until(lambda: [row.name for row in read_details_rows(self.app.panes.a.file_tree_view)] == ["sub"])


class TestLiveUpdateCatchUpOnTabSwitch(ExpLorerFreshAppTestCase):
    def test_switching_to_a_background_tab_catches_it_up_on_what_it_missed(self, tmp_path: Path) -> None:
        """Only the ACTIVE tab of each currently-VISIBLE pane is watched (Architecture.md §14.33) --
        a second tab in the same (single, visible) pane is not live-updated while backgrounded.
        Switching to it, though, re-evaluates `DirectoryWatchService::setVisibleTabs`, which
        watches *and* immediately refreshes any newly-visible tab to catch up on anything missed
        while it wasn't -- so the change must still show up right away once it becomes active,
        without a manual refresh.
        """
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(second))
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["first", "second"])

        # Switch back to the first tab (now backgrounded) before the change happens.
        self.app.panes.a.tabs.select(0)
        wait_until(lambda: self.app.navigation.address_bar.text == str(first))
        assert read_details_rows(self.app.panes.a.file_tree_view) == []

        # Make the SECOND tab (not the one we just switched to) the active/visible one again,
        # then modify the folder behind the now-backgrounded first tab.
        self.app.panes.a.tabs.select(1)
        wait_until(lambda: self.app.navigation.address_bar.text == str(second))

        (first / "missed_while_inactive.txt").write_text("x")

        self.app.panes.a.tabs.select(0)
        wait_until(lambda: self.app.navigation.address_bar.text == str(first))
        rows = wait_until(lambda: read_details_rows(self.app.panes.a.file_tree_view) or None)
        assert [row.name for row in rows] == ["missed_while_inactive.txt"]
