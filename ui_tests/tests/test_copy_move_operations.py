"""Coverage for the custom copy/move pipeline and its progress-notification UI
(`FileOperationsController::pasteInto` -> `FileTransferService`, Architecture.md §14.47) --
context-menu-triggered transfers of several files, the progress card appearing/disappearing
around a many-file copy, the "Copy Complete" completion toast, best-effort mid-copy cancellation,
and the auto-rename-on-collision behavior `pasteInto` actually implements (no skip/overwrite/
rename prompt -- see `uniqueDestinationName` in `FileOperationsController.cpp`). Compare
`test_clipboard_operations.py`, which covers the same underlying controller methods for small,
single/few-file keyboard-driven transfers without asserting on the notification UI.

The progress/cancel tests transfer many small files rather than one large one -- confirmed
against a real build that this machine's disk is fast enough (page-cache-backed, even for a
multi-GB single file immediately after writing it) that a single big file's copy completes
before a single poll can ever observe the progress card; many small files make the transfer's
per-file overhead (not raw byte throughput) dominate, reliably stretching it to a few seconds.
"""

from __future__ import annotations

from pathlib import Path

from framework.app import ExpLorerApp
from framework.context_menu import open_context_menu
from framework.details_view import copy_selection, cut_selection, paste_clipboard, row_item, select_all, select_row
from framework.wait import wait_until

_MANY_FILES_COUNT = 800


def _write_many_small_files(directory: Path, count: int = _MANY_FILES_COUNT) -> None:
    for i in range(count):
        (directory / f"many_{i}.txt").write_text(str(i))


def test_context_menu_copy_and_paste_produces_many_small_files_at_destination(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    names = [f"file_{i}.txt" for i in range(10)]
    for name in names:
        (source_dir / name).write_text(name)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, names[0])
    for name in names[1:]:
        select_row(tree, name, additive=True)
    row = row_item(tree, names[0])
    menu = open_context_menu(row)
    menu.click("Copy")

    app.navigation.navigate_to(str(dest_dir))
    background_menu = open_context_menu(app.panes.a.file_tree_view.raw)
    background_menu.click("Paste")

    wait_until(lambda: all((dest_dir / name).is_file() for name in names))
    assert all((source_dir / name).is_file() for name in names)


def test_context_menu_cut_and_paste_moves_many_small_files(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    names = [f"file_{i}.txt" for i in range(10)]
    for name in names:
        (source_dir / name).write_text(name)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, names[0])
    for name in names[1:]:
        select_row(tree, name, additive=True)
    row = row_item(tree, names[0])
    menu = open_context_menu(row)
    menu.click("Cut")

    app.navigation.navigate_to(str(dest_dir))
    background_menu = open_context_menu(app.panes.a.file_tree_view.raw)
    background_menu.click("Paste")

    wait_until(lambda: all((dest_dir / name).is_file() for name in names))
    assert not any((source_dir / name).exists() for name in names)


def test_progress_notification_appears_during_a_large_copy_and_disappears_on_completion(
    app: ExpLorerApp, tmp_path: Path
) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    _write_many_small_files(source_dir)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_all(tree)
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(app.notifications.progress_visible, timeout=5)
    wait_until(lambda: not app.notifications.progress_visible(), timeout=60)
    wait_until(lambda: len(list(dest_dir.iterdir())) == _MANY_FILES_COUNT)


def test_completion_toast_shows_after_a_copy(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    (source_dir / "a.txt").write_text("x")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "a.txt")
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: (dest_dir / "a.txt").is_file())
    wait_until(lambda: app.notifications.any_toast_contains("Copy Complete"), timeout=4)


def test_cancel_mid_copy_leaves_a_consistent_state(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    _write_many_small_files(source_dir)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_all(tree)
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(app.notifications.progress_visible, timeout=5)
    app.notifications.cancel_progress()

    wait_until(lambda: not app.notifications.progress_visible(), timeout=30)
    # No crash, and the source is untouched regardless of how much of the destination landed.
    assert app.exists()
    assert len(list(source_dir.iterdir())) == _MANY_FILES_COUNT


def test_pasting_a_name_collision_auto_renames_rather_than_overwriting(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    (source_dir / "a.txt").write_text("source content")
    (dest_dir / "a.txt").write_text("existing content")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "a.txt")
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: (dest_dir / "a (2).txt").is_file())
    assert (dest_dir / "a.txt").read_text() == "existing content"
    assert (dest_dir / "a (2).txt").read_text() == "source content"
