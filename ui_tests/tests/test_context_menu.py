"""Coverage for the custom right-click context menu
(`FileItemContextMenu::show`/`WorkspacePaneWidget::showBackgroundContextMenu`, Architecture.md
§14.13) in the Details view -- a file row, a folder row, and empty background space each produce
a menu, with different entries for an item vs. empty space; invoking "Rename"/"Delete" from the
menu goes through the same inline-edit/confirmation paths the F2/Delete keyboard shortcuts do
(see `test_details_view_rename_delete.py`, the template for both). Registry-sourced shell
entries aren't asserted on here -- only the small subset of app-owned entries that must always be
present, since the rest is machine/registry-dependent.
"""

from __future__ import annotations

import time
from pathlib import Path

from framework.app import ExpLorerApp
from framework.context_menu import open_context_menu
from framework.details_view import commit_rename, confirm_delete_dialog, row_item, select_row
from framework.wait import wait_until

_ITEM_ENTRIES = {"Open", "Rename", "Delete", "Copy", "Cut", "Properties"}
_BACKGROUND_ENTRIES = {"New Folder", "Paste"}
# "Properties" is legitimately on both menus (WorkspacePaneWidget::showBackgroundContextMenu
# includes it too) -- excluded here so the background menu's "doesn't show item-only entries"
# check isn't tripped by the one entry both menus share.
_ITEM_ONLY_ENTRIES = _ITEM_ENTRIES - {"Properties"}


def test_right_click_on_a_file_row_shows_the_core_item_entries(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "a.txt").write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    row = row_item(app.panes.a.file_tree_view, "a.txt")
    menu = open_context_menu(row)
    try:
        assert _ITEM_ENTRIES <= set(menu.item_texts())
    finally:
        menu.close()


def test_right_click_on_a_folder_row_shows_the_core_item_entries(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "sub").mkdir()
    app.navigation.navigate_to(str(tmp_path))

    row = row_item(app.panes.a.file_tree_view, "sub")
    menu = open_context_menu(row)
    try:
        assert _ITEM_ENTRIES <= set(menu.item_texts())
    finally:
        menu.close()


def test_right_click_on_empty_background_shows_a_different_generic_menu(app: ExpLorerApp, tmp_path: Path) -> None:
    app.navigation.navigate_to(str(tmp_path))  # empty folder -- every point is "background"

    menu = open_context_menu(app.panes.a.file_tree_view.raw)
    try:
        texts = set(menu.item_texts())
        assert _BACKGROUND_ENTRIES <= texts
        assert not (_ITEM_ONLY_ENTRIES & texts)
    finally:
        menu.close()


def test_multi_selection_right_click_still_produces_a_menu(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "a.txt").write_text("a")
    (tmp_path / "b.txt").write_text("b")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "a.txt")
    select_row(tree, "b.txt", additive=True)

    row = row_item(tree, "b.txt")
    menu = open_context_menu(row)
    try:
        texts = set(menu.item_texts())
        assert {"Copy", "Cut", "Delete"} <= texts
    finally:
        menu.close()


def test_rename_from_the_menu_enters_the_same_inline_edit_state_as_f2(app: ExpLorerApp, tmp_path: Path) -> None:
    (tmp_path / "old_name.txt").write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "old_name.txt")
    row = row_item(tree, "old_name.txt")
    menu = open_context_menu(row)
    menu.click("Rename")
    time.sleep(0.3)  # lets the editor actually open before keys start arriving

    # Same inline editor F2 opens (FileBrowserView::beginRename -> QAbstractItemView::edit()) --
    # see `details_view.begin_rename`'s docstring for why this can't be located/asserted on
    # directly via UI Automation, only driven blindly by keystrokes like this.
    commit_rename("new_name.txt")

    wait_until(lambda: (tmp_path / "new_name.txt").is_file())
    assert not (tmp_path / "old_name.txt").exists()


def test_delete_from_the_menu_goes_through_the_same_confirmation_as_the_delete_key(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "doomed.txt"
    target.write_text("x")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "doomed.txt")
    row = row_item(tree, "doomed.txt")
    menu = open_context_menu(row)
    menu.click("Delete")

    confirm_delete_dialog(permanent=False, accept=True)
    wait_until(lambda: not target.exists())
