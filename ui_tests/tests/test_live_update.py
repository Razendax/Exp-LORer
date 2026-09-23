"""Coverage for the Details view refreshing live when the current folder's contents change on
disk -- driven by DirectoryWatchService's debounced watch (src/adapters/viewmodels/
DirectoryWatchService.h, ~400ms debounce), not a manual refresh click.
"""

from __future__ import annotations

from framework.app import ExpLorerApp
from framework.details_view import read_details_rows
from framework.wait import wait_until


def test_new_file_appears_without_manual_refresh(app: ExpLorerApp, tmp_path):
    target = tmp_path / "watched"
    target.mkdir()
    app.navigation.navigate_to(str(target))
    assert read_details_rows(app.panes.a.file_tree_view) == []

    (target / "new.txt").write_text("hello")

    rows = wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) or None)
    assert [row.name for row in rows] == ["new.txt"]


def test_deleted_file_disappears_without_manual_refresh(app: ExpLorerApp, tmp_path):
    target = tmp_path / "watched"
    target.mkdir()
    doomed = target / "doomed.txt"
    doomed.write_text("bye")

    app.navigation.navigate_to(str(target))
    wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) or None)

    doomed.unlink()

    wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) == [])
    assert read_details_rows(app.panes.a.file_tree_view) == []


def test_modified_file_size_updates_without_manual_refresh(app: ExpLorerApp, tmp_path):
    target = tmp_path / "watched"
    target.mkdir()
    growing = target / "growing.txt"
    growing.write_text("a")

    app.navigation.navigate_to(str(target))
    wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) or None)

    growing.write_text("a" * 10)

    def grown_to_ten_bytes():
        rows = read_details_rows(app.panes.a.file_tree_view)
        return rows and rows[0].size == "10"

    wait_until(grown_to_ten_bytes)


def test_new_subfolder_appears_without_manual_refresh(app: ExpLorerApp, tmp_path):
    target = tmp_path / "watched"
    target.mkdir()
    app.navigation.navigate_to(str(target))
    assert read_details_rows(app.panes.a.file_tree_view) == []

    (target / "new_subfolder").mkdir()

    rows = wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) or None)
    assert rows[0].name == "new_subfolder"
    assert rows[0].type == "File folder"
