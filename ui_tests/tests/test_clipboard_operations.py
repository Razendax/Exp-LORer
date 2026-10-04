"""Coverage for keyboard-driven clipboard file operations in the Details view
(`FileBrowserView::handleKeyPress`'s Ctrl+C/Ctrl+X/Ctrl+V handling ->
`FileOperationsController::copyToClipboard`/`cutToClipboard`/`pasteInto`, Architecture.md §14.10)
-- copy/cut a selection, paste it elsewhere, and the no-op edge cases around an empty or
stale clipboard. Compare `test_context_menu.py` (same underlying controller methods, triggered
via the right-click menu instead of keyboard shortcuts) and `test_copy_move_operations.py`
(larger transfers and the progress-notification UI this app shows while a copy/move is in
flight).

Uses the `app` fixture (fresh Exp-LORer instance per test) -- these tests drive the real OS
clipboard, which persists across test methods if a shared instance's clipboard state were ever
read, so a fresh process per test keeps each test's starting clipboard state unambiguous.
"""

from __future__ import annotations

from pathlib import Path

import win32clipboard

from framework.app import ExpLorerApp
from framework.details_view import copy_selection, cut_selection, paste_clipboard, select_row
from framework.wait import wait_until


def _clear_os_clipboard() -> None:
    """Empties the real Windows clipboard so a leftover file list from outside this test suite
    can't make a "paste with nothing copied" assertion flaky.
    """
    win32clipboard.OpenClipboard()
    win32clipboard.EmptyClipboard()
    win32clipboard.CloseClipboard()


def test_copy_then_paste_leaves_the_file_in_both_places_unchanged(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    source_file = source_dir / "a.txt"
    source_file.write_text("hello")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "a.txt")
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: (dest_dir / "a.txt").is_file())
    assert source_file.is_file()
    assert (dest_dir / "a.txt").read_text() == "hello"


def test_cut_then_paste_moves_the_file(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    source_file = source_dir / "b.txt"
    source_file.write_text("world")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "b.txt")
    cut_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: (dest_dir / "b.txt").is_file())
    assert not source_file.exists()


def test_copy_multi_selection_transfers_every_item(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    for name in ("one.txt", "two.txt", "three.txt"):
        (source_dir / name).write_text(name)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "one.txt")
    select_row(tree, "two.txt", additive=True)
    select_row(tree, "three.txt", additive=True)
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: all((dest_dir / name).is_file() for name in ("one.txt", "two.txt", "three.txt")))
    assert all((source_dir / name).is_file() for name in ("one.txt", "two.txt", "three.txt"))


def test_cut_multi_selection_transfers_every_item(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    for name in ("one.txt", "two.txt"):
        (source_dir / name).write_text(name)

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "one.txt")
    select_row(tree, "two.txt", additive=True)
    cut_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: all((dest_dir / name).is_file() for name in ("one.txt", "two.txt")))
    assert not (source_dir / "one.txt").exists()
    assert not (source_dir / "two.txt").exists()


def test_paste_with_no_prior_copy_or_cut_is_a_noop(app: ExpLorerApp, tmp_path: Path) -> None:
    _clear_os_clipboard()
    target = tmp_path / "empty_clip_target"
    target.mkdir()

    app.navigation.navigate_to(str(target))
    paste_clipboard(app.panes.a.file_tree_view)

    # No crash, and nothing materialized -- then confirm the app is still responsive.
    assert list(target.iterdir()) == []
    assert app.exists()
    assert app.navigation.address_bar.text == str(target)


def test_paste_into_the_same_folder_a_cut_item_came_from_is_a_noop(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    source_dir.mkdir()
    source_file = source_dir / "stay.txt"
    source_file.write_text("stay")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, "stay.txt")
    cut_selection(tree)
    paste_clipboard(tree)

    assert source_file.is_file()
    assert source_file.read_text() == "stay"
    assert list(source_dir.iterdir()) == [source_file]


def test_non_ascii_filename_round_trips_through_clipboard(app: ExpLorerApp, tmp_path: Path) -> None:
    source_dir = tmp_path / "source"
    dest_dir = tmp_path / "dest"
    source_dir.mkdir()
    dest_dir.mkdir()
    name = "Папка_日本語_😀.txt"
    source_file = source_dir / name
    source_file.write_text("x")

    app.navigation.navigate_to(str(source_dir))
    tree = app.panes.a.file_tree_view
    select_row(tree, name)
    copy_selection(tree)

    app.navigation.navigate_to(str(dest_dir))
    paste_clipboard(app.panes.a.file_tree_view)

    wait_until(lambda: (dest_dir / name).is_file())
    assert source_file.is_file()
