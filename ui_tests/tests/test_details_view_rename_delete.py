"""Coverage for in-view rename (F2) and delete (Delete/Shift+Delete) directly in the Details
view (`FileBrowserView::handleKeyPress`, `FileNameEditDelegate`, `FileItemContextMenu::
confirmAndDelete`, Architecture.md §14.13's gap note) -- compare `test_left_panel_tree.py`'s
`FolderTree.rename`, the equivalent inline rename in the folder tree, which this mirrors, and
`test_context_menu.py`, which drives the same rename/delete entry points via the right-click menu
instead of these keyboard shortcuts.

Uses the `app` fixture (fresh instance per test) since these tests assert on exact pre/post
filesystem state for a specific file -- a shared instance would work too, but a fresh one keeps
each test's `tmp_path` the only state in play.
"""

from __future__ import annotations

import time
from pathlib import Path

from framework.app import ExpLorerApp
from framework.details_view import (
    begin_rename,
    cancel_rename,
    commit_rename,
    confirm_delete_dialog,
    delete_selection,
    rename,
    select_row,
)
from framework.wait import wait_until


def _select_tab_with_retry(pane, index: int, attempts: int = 5) -> None:
    """`PaneTabs.select` eagerly wraps every descendant of the tab widget, which can raise a
    transient `KeyError` from pywinauto itself (not this app) if a tab is still settling right
    after another tab's folder was deleted out from under it. Retry rather than treat that as a
    real failure.
    """
    last_exc: Exception = RuntimeError("unreachable")
    for _ in range(attempts):
        try:
            pane.tabs.select(index)
            return
        except Exception as exc:  # noqa: BLE001 -- see docstring
            last_exc = exc
            time.sleep(0.3)
    raise last_exc


def test_f2_rename_commits_with_enter(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "old_name.txt").write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    rename(app.panes.a.file_tree_view, "old_name.txt", "new_name.txt")

    wait_until(lambda: (tmp_path / "new_name.txt").is_file())
    assert not (tmp_path / "old_name.txt").exists()


def test_f2_rename_commits_with_enter_for_a_folder(app: ExpLorerApp, tmp_path: Path) -> None:
    """Folder counterpart of `test_f2_rename_commits_with_enter` above -- regression coverage for
    a confirmed production bug (unrelated to the hotkey feature, found incidentally while adding
    that coverage): renaming a FOLDER via F2 was a silent no-op, while the identical mechanism on a
    file always worked. Root cause: pressing Enter in the inline editor doesn't synchronously commit
    the edit -- Qt's own item-delegate commit-on-Enter handling
    (QAbstractItemDelegatePrivate::editorEventFilter) deliberately defers the actual commit to a
    queued call so the still-focused editor gets to validate the input first, and the same Enter
    keypress, left unconsumed by QLineEdit (which itself ignores it, by design, so a dialog's
    default button can still react to it), bubbles up to `FileBrowserView`'s own key handling as a
    plain "activate the current selection" -- which, for a directory, navigated into it and reset
    the model before the still-pending queued commit ever ran, silently dropping the rename. A file
    has no such disruptive side effect from being "activated" mid-rename, which is why only folders
    were affected. Fixed by `FileBrowserView::eventFilter` only treating a KeyPress as targeting the
    view itself (vs. one bubbled up from a focused child editor) when the view actually has focus.
    """
    (tmp_path / "old_folder").mkdir()
    app.navigation.navigate_to(str(tmp_path))

    rename(app.panes.a.file_tree_view, "old_folder", "new_folder")

    wait_until(lambda: (tmp_path / "new_folder").is_dir())
    assert not (tmp_path / "old_folder").exists()


def test_escape_cancels_rename(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "keep_me.txt").write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    begin_rename(app.panes.a.file_tree_view, "keep_me.txt")
    cancel_rename()

    assert (tmp_path / "keep_me.txt").is_file()


def test_empty_name_is_rejected(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "keep_me.txt").write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    begin_rename(app.panes.a.file_tree_view, "keep_me.txt")
    commit_rename("")

    assert (tmp_path / "keep_me.txt").is_file()


def test_renaming_to_an_existing_sibling_name_is_rejected(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "a.txt").write_text("a")
    (tmp_path / "b.txt").write_text("b")
    app.navigation.navigate_to(str(tmp_path))

    begin_rename(app.panes.a.file_tree_view, "a.txt")
    commit_rename("b.txt")

    assert (tmp_path / "a.txt").is_file()
    assert (tmp_path / "b.txt").read_text() == "b"


def test_delete_key_moves_file_to_trash(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "doomed.txt"
    target.write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "doomed.txt")
    delete_selection(tree, permanent=False)
    confirm_delete_dialog(permanent=False, accept=True)

    wait_until(lambda: not target.exists())


def test_shift_delete_permanently_removes_file(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "doomed.txt"
    target.write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "doomed.txt")
    delete_selection(tree, permanent=True)
    confirm_delete_dialog(permanent=True, accept=True)

    wait_until(lambda: not target.exists())


def test_declining_the_delete_confirmation_leaves_the_file_untouched(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "safe.txt"
    target.write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "safe.txt")
    delete_selection(tree, permanent=False)
    confirm_delete_dialog(permanent=False, accept=False)

    assert target.is_file()


def test_deleting_the_active_tabs_current_folder_navigates_it_up_gracefully(app: ExpLorerApp, tmp_path: Path) -> None:
    parent = tmp_path / "parent"
    child = parent / "child"
    child.mkdir(parents=True)

    # Tab 0 browses the folder that's about to be deleted out from under it.
    app.navigation.navigate_to(str(child))

    # Tab 1 (a second tab in the same pane) deletes it from the parent's listing.
    app.navigation.new_tab.click()
    app.navigation.navigate_to(str(parent))
    tree = app.panes.a.file_tree_view
    select_row(tree, "child")
    delete_selection(tree, permanent=True)
    confirm_delete_dialog(permanent=True, accept=True)
    wait_until(lambda: not child.exists())

    # Switching back to tab 0 shows it was moved up to the parent rather than left dangling/crashed.
    _select_tab_with_retry(app.panes.a, 0)
    wait_until(lambda: app.navigation.address_bar.text == str(parent))
    assert app.exists()


def test_non_ascii_name_rename_and_delete(app: ExpLorerApp, tmp_path: Path) -> None:
    # The original name is created directly on disk (no typing involved), so it can safely
    # include an emoji; the renamed-to name below is typed via keystrokes (see `commit_rename`'s
    # docstring), which can't reliably send emoji -- Cyrillic/CJK text is enough to cover "non-ASCII".
    original = "Папка_日本語_😀.txt"
    renamed = "新しい名前_テスト.txt"
    (tmp_path / original).write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    rename(app.panes.a.file_tree_view, original, renamed)
    wait_until(lambda: (tmp_path / renamed).is_file())

    tree = app.panes.a.file_tree_view
    select_row(tree, renamed)
    delete_selection(tree, permanent=True)
    confirm_delete_dialog(permanent=True, accept=True)
    wait_until(lambda: not (tmp_path / renamed).exists())
