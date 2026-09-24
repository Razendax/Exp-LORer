"""Page object for `TagManagerDialog` (Architecture.md §14.17, opened from Edit > "Tag Edit...")
-- the standalone dialog for creating/renaming/deleting tags globally, independent of any
file/folder selection (compare `right_panel.TagPanel`, which tags the current selection).
"""

from __future__ import annotations

from typing import Any

import win32gui

from .automation_id import AutomationID
from .element import Element
from .modal_dialog import connect_by_handle, find_control, wait_for_second_window_handle, wait_for_window_handle
from .tag_chip import chip_names, find_chip


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
        Rename. Use `rename_tag_expect_failure` instead when the rename is expected to fail.
        """
        input_dlg = self.open_rename_dialog(old_name)
        find_control(input_dlg, "Edit").set_edit_text(new_name)
        find_control(input_dlg, "Button", "OK").click_input()

    def rename_tag_expect_failure(self, old_name: str, new_name: str) -> None:
        input_dlg = self.open_rename_dialog(old_name)
        find_control(input_dlg, "Edit").set_edit_text(new_name)
        find_control(input_dlg, "Button", "OK").click_input()
        self.dismiss_warning()

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
