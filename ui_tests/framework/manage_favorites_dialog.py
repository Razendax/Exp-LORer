"""Page object for `ManageFavoritesDialog` (Architecture.md §14.35, opened from Favorites >
"Manage Favorites...") -- the standalone two-level tree (categories -> items) for adding
categories, renaming them, reordering/removing entries. Compare `favorites_menu.py`'s
`add_to_favorites`, which is the *other* way to add a favorite (the current selection/browsed
folder, with no dialog involved).

Like `TagManagerDialog`, this dialog has no persistent automation id path from the main window
(it's a `dialog.exec()`-ed top-level window created on demand), so it's opened by clicking through
the menu bar and then located by HWND rather than by title/tree search -- see `modal_dialog.py`
for why.
"""

from __future__ import annotations

import time
from typing import Any, List, Optional

import win32gui

from .automation_id import AutomationID
from .element import Element
from .modal_dialog import connect_by_handle, find_control, wait_for_window_handle


class ManageFavoritesDialog:
    def __init__(self, hwnd: int):
        self.hwnd = hwnd
        self._window = connect_by_handle(hwnd)
        self.tree = Element(self._container, AutomationID.MANAGE_FAVORITES_TREE)
        self.add_file_button = Element(self._container, AutomationID.MANAGE_FAVORITES_ADD_FILE_BUTTON)
        self.add_folder_button = Element(self._container, AutomationID.MANAGE_FAVORITES_ADD_FOLDER_BUTTON)
        self.add_category_button = Element(self._container, AutomationID.MANAGE_FAVORITES_ADD_CATEGORY_BUTTON)
        self.rename_button = Element(self._container, AutomationID.MANAGE_FAVORITES_RENAME_BUTTON)
        self.move_to_category_button = Element(self._container, AutomationID.MANAGE_FAVORITES_MOVE_TO_CATEGORY_BUTTON)
        self.move_up_button = Element(self._container, AutomationID.MANAGE_FAVORITES_MOVE_UP_BUTTON)
        self.move_down_button = Element(self._container, AutomationID.MANAGE_FAVORITES_MOVE_DOWN_BUTTON)
        self.remove_button = Element(self._container, AutomationID.MANAGE_FAVORITES_REMOVE_BUTTON)
        self.close_button = Element(self._container, AutomationID.MANAGE_FAVORITES_CLOSE_BUTTON)

    def _container(self) -> Any:
        return self._window

    def exists(self) -> bool:
        return bool(win32gui.IsWindow(self.hwnd))

    def entry_names(self, timeout: float = 5) -> List[str]:
        """Every tree row's name (both categories and items, top-level and nested under a
        category), depth-first in display order -- rebuilt wholesale by
        `ManageFavoritesDialog::rebuildTree` on every `favoritesChanged()`, so this is always a
        full, current snapshot rather than needing per-row polling.
        """
        deadline = time.monotonic() + timeout
        last_error: Optional[BaseException] = None
        while time.monotonic() < deadline:
            try:
                return [
                    item.window_text()
                    for item in self.tree.raw.descendants()
                    if item.element_info.control_type == "TreeItem"
                ]
            except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
                last_error = exc
            time.sleep(0.15)
        raise LookupError(f"Could not read tree rows from {self.tree!r} ({last_error})")

    def select_entry(self, name: str, timeout: float = 5) -> None:
        """Clicks the tree row named `name` (a category or an item, at any depth), selecting it --
        drives `onSelectionChanged`, which enables/disables Rename/Move/Remove for the new
        selection.
        """
        deadline = time.monotonic() + timeout
        last_error: Optional[BaseException] = None
        while time.monotonic() < deadline:
            try:
                for item in self.tree.raw.descendants():
                    if item.element_info.control_type == "TreeItem" and item.window_text() == name:
                        item.click_input()
                        return
            except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
                last_error = exc
            time.sleep(0.15)
        raise LookupError(f"No tree row named {name!r} to select ({last_error})")

    def add_category(self, name: str) -> None:
        """Clicks "Add Category" and fills/accepts the `QInputDialog::getText` it opens."""
        self.add_category_button.click()
        input_dlg = connect_by_handle(wait_for_window_handle("Add Category"))
        find_control(input_dlg, "Edit").set_edit_text(name)
        find_control(input_dlg, "Button", "OK").click_input()

    def rename_category(self, old_name: str, new_name: str) -> None:
        """Selects the `old_name` category row, clicks Rename, and fills/accepts the
        `QInputDialog::getText` it opens.
        """
        self.select_entry(old_name)
        self.rename_button.click()
        input_dlg = connect_by_handle(wait_for_window_handle("Rename Category"))
        find_control(input_dlg, "Edit").set_edit_text(new_name)
        find_control(input_dlg, "Button", "OK").click_input()

    def remove_entry(self, name: str) -> None:
        """Selects the row named `name` (category or item, no confirmation prompt either way --
        `onRemoveClicked` removes immediately) and clicks Remove.
        """
        self.select_entry(name)
        self.remove_button.click()

    def move_entry_up(self, name: str) -> None:
        self.select_entry(name)
        self.move_up_button.click()

    def move_entry_down(self, name: str) -> None:
        self.select_entry(name)
        self.move_down_button.click()

    def close(self) -> None:
        self.close_button.click()
