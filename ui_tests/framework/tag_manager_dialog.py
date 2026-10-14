"""Page object for `TagManagerDialog` (Architecture.md §14.17, opened from Edit > "Tag Edit...")
-- the standalone dialog for creating/renaming/deleting tags globally, independent of any
file/folder selection (compare `right_panel.TagPanel`, which tags the current selection).
"""

from __future__ import annotations

from typing import Any, List

import win32gui

from .automation_id import AutomationID
from .element import Element
from .modal_dialog import connect_by_handle, find_control, wait_for_second_window_handle, wait_for_window_handle
from .tag_chip import chip_count_text, chip_names, find_chip

# Fixed display order of TagManagerDialog.cpp's combo boxes -- used by select_combo_item (see
# combo_box.py's own docstring for why Qt combo boxes need this pixel-position approach rather
# than a name/index-based `select()` call).
_SORT_KEY_ORDER = ["Alphabetical", "Count"]
_SORT_ORDER_ORDER = ["Ascending", "Descending"]
_VIEW_MODE_ORDER = ["List", "Tree"]


def open_tag_manager_dialog(app: Any) -> "TagManagerDialog":
    """Opens Edit > "Tag Edit..." and returns a page object bound to the resulting dialog.

    Clicks through the native menu bar by title (`MainWindow.cpp`'s `&Tag Edit...` QAction has no
    backing widget/automation id -- `automation_id.py`'s own docs recommend exactly this "click
    through the menu directly" approach for such entries) rather than pywinauto's `menu_select`,
    which fails against this app's menu items (observed: a COM `E_UNEXPECTED` from pywinauto's
    SelectionItem/Invoke pattern lookup -- Qt's accessibility bridge doesn't expose a usable
    pattern for these QAction-backed QMenu items, only plain hit-testable rectangles). See
    `modal_dialog.py` for why the dialog itself is then located by HWND rather than by title.
    """
    app.window.child_window(title="Edit", control_type="MenuItem").click_input()
    app.window.child_window(title="Tag Edit...", control_type="MenuItem").click_input()
    hwnd = wait_for_window_handle("Tag Manager")
    return TagManagerDialog(hwnd)


class TagManagerDialog:
    def __init__(self, hwnd: int):
        self.hwnd = hwnd
        self._window = connect_by_handle(hwnd)
        self.search_edit = Element(self._container, AutomationID.TAG_MANAGER_SEARCH_EDIT)
        self.show_counts_button = Element(self._container, AutomationID.TAG_MANAGER_SHOW_COUNTS_BUTTON)
        self.sort_key_combo = Element(self._container, AutomationID.TAG_MANAGER_SORT_KEY_COMBO)
        self.sort_order_combo = Element(self._container, AutomationID.TAG_MANAGER_SORT_ORDER_COMBO)
        self.view_mode_combo = Element(self._container, AutomationID.TAG_MANAGER_VIEW_MODE_COMBO)
        self.tag_tree = Element(self._container, AutomationID.TAG_MANAGER_TAG_TREE)
        self.results_view = Element(self._container, AutomationID.TAG_MANAGER_RESULTS_VIEW)
        self._results_tree_view = Element(lambda: self.results_view.raw, AutomationID.FILE_TREE_VIEW)
        self.rename_button = Element(self._container, AutomationID.TAG_MANAGER_RENAME_BUTTON)
        self.add_button = Element(self._container, AutomationID.TAG_MANAGER_ADD_BUTTON)
        self.delete_button = Element(self._container, AutomationID.TAG_MANAGER_DELETE_BUTTON)
        self.close_button = Element(self._container, AutomationID.TAG_MANAGER_CLOSE_BUTTON)

    def _container(self) -> Any:
        return self._window

    def exists(self) -> bool:
        return bool(win32gui.IsWindow(self.hwnd))

    def chip_names(self) -> list:
        """Every tag chip currently shown (i.e. matching the current search query), in display
        order.
        """
        return chip_names(self._window)

    def click_chip(self, tag_name: str) -> None:
        """Selects the chip named `tag_name` -- enables Rename/Delete (`onChipClicked`)."""
        find_chip(self._window, tag_name).click()

    def chip_count_text(self, tag_name: str) -> Any:
        """The `tag_name` chip's count label text (e.g. "3" or "3/7" for a nested tag with
        children), or `None` if the "#" show-counts toggle is off. See `tag_chip.chip_count_text`.
        """
        return chip_count_text(self._window, tag_name)

    def select_sort_key(self, key: str) -> None:
        """Switches the sort-key combo to `key` ("Alphabetical" or "Count",
        `TagManagerViewModel::SortKey`)."""
        from .combo_box import select_combo_item

        select_combo_item(self.sort_key_combo.raw, key, _SORT_KEY_ORDER)

    def select_sort_order(self, order: str) -> None:
        """Switches the sort-order combo to `order` ("Ascending" or "Descending",
        `TagManagerViewModel::SortOrder`)."""
        from .combo_box import select_combo_item

        select_combo_item(self.sort_order_combo.raw, order, _SORT_ORDER_ORDER)

    def select_view_mode(self, mode: str) -> None:
        """Switches the view-mode combo to `mode` ("List" or "Tree") -- `onViewModeChanged` swaps
        which widget `m_tagAreaStack` shows (the chip flow vs. the nested `m_tagTree`)."""
        from .combo_box import select_combo_item

        select_combo_item(self.view_mode_combo.raw, mode, _VIEW_MODE_ORDER)

    def tree_item_texts(self) -> List[str]:
        """Every tag tree item's text (its path segment, plus a "  (N)"/"  (N, M total)" count
        suffix when the "#" show-counts toggle is on), depth-first in display order -- rebuilt
        wholesale by `rebuildTagTree` on every `matchingTagsChanged`. The tree is always fully
        expanded (`m_tagTree->expandAll()`), so this is always a complete, current snapshot.
        """
        return [item.window_text() for item in self.tag_tree.raw.descendants(control_type="TreeItem")]

    def select_tree_item(self, text: str) -> None:
        """Clicks the tag tree item whose exact text matches `text` (see `tree_item_texts` for the
        count-suffix caveat when show-counts is on), selecting it -- `onTreeSelectionChanged`
        enables Rename/Delete for a real tag node, or clears the selection for a virtual
        path-grouping node (one with no tag of its own, e.g. "Project" when only "Project/Design"
        is an actual tag).
        """
        for item in self.tag_tree.raw.descendants(control_type="TreeItem"):
            if item.window_text() == text:
                item.click_input()
                return
        raise LookupError(f"No tag tree item {text!r} among {self.tree_item_texts()}")

    def result_names(self) -> List[str]:
        """Display names of every file/folder currently shown in the results view below the tag
        area -- the files/folders tagged with whichever chip/tree item is currently selected
        (`TagManagerViewModel::filesForTagChanged`); empty once nothing is selected.
        """
        from .details_view import read_name_column

        return read_name_column(self._results_tree_view)

    def open_add_dialog(self) -> Any:
        """Clicks Add and returns the raw connected `QInputDialog::getText` window -- the escape
        hatch for corner cases `add_tag`/`add_tag_expect_failure` don't cover (Cancel, an
        empty/whitespace-only name). Use `modal_dialog.find_control` to interact with it.
        """
        self.add_button.click()
        return connect_by_handle(wait_for_window_handle("Add Tag"))

    def open_rename_dialog(self, old_name: str) -> Any:
        """Selects the `old_name` chip, clicks Rename, and returns the raw connected
        `QInputDialog::getText` window (see `open_add_dialog`)."""
        self.click_chip(old_name)
        self.rename_button.click()
        return connect_by_handle(wait_for_window_handle("Rename Tag"))

    def add_tag(self, name: str) -> None:
        """Fills and accepts the `QInputDialog::getText` opened by the Add button. Use
        `add_tag_expect_failure` instead when the add is expected to fail (e.g. a duplicate
        name), which shows a warning popup instead of just closing.
        """
        input_dlg = self.open_add_dialog()
        find_control(input_dlg, "Edit").set_edit_text(name)
        find_control(input_dlg, "Button", "OK").click_input()

    def add_tag_expect_failure(self, name: str) -> None:
        input_dlg = self.open_add_dialog()
        find_control(input_dlg, "Edit").set_edit_text(name)
        find_control(input_dlg, "Button", "OK").click_input()
        self.dismiss_warning()

    def rename_tag(self, old_name: str, new_name: str) -> None:
        """Selects the `old_name` chip and fills/accepts the `QInputDialog::getText` opened by
        Rename. `new_name` must not collide (case-insensitively) with any other existing tag's
        name -- use `rename_tag_merge` instead for that case, which answers the "Merge Tags"
        prompt this dialog shows instead of applying the rename directly.
        """
        input_dlg = self.open_rename_dialog(old_name)
        find_control(input_dlg, "Edit").set_edit_text(new_name)
        find_control(input_dlg, "Button", "OK").click_input()

    def rename_tag_merge(self, old_name: str, new_name: str, accept: bool = True) -> None:
        """Renames `old_name` to `new_name` where `new_name` already names a different tag --
        `onRenameClicked`'s case-insensitive collision check routes this into a
        `QMessageBox::question` titled "Merge Tags" instead of a plain rename, asking whether to
        fold `old_name`'s tag associations into the existing `new_name` tag (removing `old_name`)
        rather than rejecting the rename outright. Answers it "Yes" (merge) if `accept`, else "No"
        (leaves both tags untouched, same as Cancel).
        """
        input_dlg = self.open_rename_dialog(old_name)
        find_control(input_dlg, "Edit").set_edit_text(new_name)
        find_control(input_dlg, "Button", "OK").click_input()
        merge_dlg = connect_by_handle(wait_for_window_handle("Merge Tags"))
        button_text = "Yes" if accept else "No"
        find_control(merge_dlg, "Button", button_text).click_input()

    def delete_tag(self, name: str, confirm: bool = True) -> None:
        """Selects the `name` chip, clicks Delete, and answers the `QMessageBox::question`
        confirmation prompt ("Yes" if `confirm`, else "No" -- which leaves the tag untouched).
        """
        self.click_chip(name)
        self.delete_button.click()
        confirm_dlg = connect_by_handle(wait_for_window_handle("Delete Tag"))
        button_text = "Yes" if confirm else "No"
        find_control(confirm_dlg, "Button", button_text).click_input()

    def dismiss_warning(self) -> None:
        """Waits for and dismisses the `QMessageBox::warning(this, tr("Tag Manager"), ...)` this
        dialog shows when an operation fails (`onOperationFailed`) -- titled "Tag Manager" like
        the dialog itself, so it's located by excluding this dialog's own hwnd.
        """
        warn_hwnd = wait_for_second_window_handle("Tag Manager", exclude_hwnd=self.hwnd)
        warn_dlg = connect_by_handle(warn_hwnd)
        find_control(warn_dlg, "Button", "OK").click_input()

    def close(self) -> None:
        self.close_button.click()
