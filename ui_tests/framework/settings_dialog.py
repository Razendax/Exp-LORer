"""Page object for `SettingsDialog` (SettingsDialog.cpp, opened from File > "Settings..."), covering
only the General > UI page's Theme combo box -- the one control the theme-setting suite needs.
Extend with more pages/controls as other suites need them, same posture as `tag_manager_dialog.py`.
"""

from __future__ import annotations

import time
from typing import Any

import win32gui

from .automation_id import AutomationID, find
from .modal_dialog import connect_by_handle, wait_for_window_handle

# Fixed display order of SettingsDialog::buildGeneralUiPage()'s Theme combo box items
# (UiTheme::Theme::Light/Dark) -- used to compute the on-screen row a given name's own dropdown
# item lands at (see select_theme()'s docstring for why that's needed instead of a name-based
# selection call).
_THEME_ORDER = ["Light", "Dark"]


def open_settings_dialog(app: Any) -> "SettingsDialog":
    """Opens File > "Settings..." and returns a page object bound to the resulting dialog.

    Clicks through the native menu bar by title rather than pywinauto's `menu_select` -- same
    reasoning as `tag_manager_dialog.open_tag_manager_dialog`: Qt's accessibility bridge doesn't
    expose a usable pattern for these QAction-backed QMenu items. See `modal_dialog.py` for why
    the dialog itself is then located by HWND rather than by title.
    """
    app.window.child_window(title="File", control_type="MenuItem").click_input()
    app.window.child_window(title="Settings...", control_type="MenuItem").click_input()
    hwnd = wait_for_window_handle("Settings")
    return SettingsDialog(hwnd)


class SettingsDialog:
    def __init__(self, hwnd: int):
        self.hwnd = hwnd
        self._window = connect_by_handle(hwnd)

    def exists(self) -> bool:
        return bool(win32gui.IsWindow(self.hwnd))

    def select_category(self, name: str) -> None:
        """Clicks the category tree's leaf item named `name` (e.g. "UI", "Colors") -- every
        top-level group (Highlight, General) is expanded by the dialog itself on open, so no
        separate expand step is needed.
        """
        tree = find(self._window, AutomationID.SETTINGS_CATEGORY_TREE)
        for item in tree.descendants(control_type="TreeItem"):
            if item.window_text() == name:
                item.click_input()
                return
        raise LookupError(f"No Settings category tree item named {name!r}")

    def current_theme(self) -> str:
        """The Theme combo box's currently displayed text ("Light" or "Dark")."""
        combo = find(self._window, AutomationID.SETTINGS_THEME_COMBO_BOX)
        return combo.selected_text()

    def select_theme(self, theme: str, attempts: int = 3) -> None:
        """Switches the Theme combo box to `theme` ("Light" or "Dark").

        Confirmed by hand against a real build: pywinauto's UIA `ComboBoxWrapper.select()` (both
        by name and by index) raises -- this Qt combo box's popup exposes no `Selection`/
        `SelectionItem` pattern pywinauto's generic path can drive. Clicking the popup item's own
        on-screen position works instead: the popup is a plain native top-level window, same width
        as the combo box, one row per item in `_THEME_ORDER`, with its *currently selected* row
        (not always row 0) aligned to the combo box's own rect -- see the `current_index` comment
        below for how that position is computed. Retries the whole open-popup-and-click sequence
        up to `attempts` times: how long the popup takes to actually become clickable after
        `click_input()` varies enough (observed hundreds of ms slower under pytest than a
        standalone script) that a single fixed sleep is flaky.
        """
        from pywinauto.mouse import click as mouse_click

        from .focus import bring_to_foreground

        combo = find(self._window, AutomationID.SETTINGS_THEME_COMBO_BOX)
        rect = combo.rectangle()
        row_height = rect.bottom - rect.top
        target_index = _THEME_ORDER.index(theme)
        # The popup aligns its CURRENTLY SELECTED row with the combo box's own rect (confirmed via
        # a captured screenshot when "Light" -- index 0 -- was selected; the asymmetric failure
        # when going the other way, Dark -> Light, confirmed it's the selected row that anchors,
        # not always row 0) -- so the offset from the combo's rect depends on both the target row
        # and whichever row is selected right now, not the target row alone.
        current_index = _THEME_ORDER.index(combo.selected_text())
        x = (rect.left + rect.right) // 2
        y = rect.top + (target_index - current_index) * row_height + row_height // 2

        for attempt in range(attempts):
            # The Settings dialog can lose OS foreground status between opening it and this call
            # (e.g. the test runner's own window stealing it back) -- re-force it right before
            # each click, same posture as details_view.py's double-click helpers.
            bring_to_foreground(combo)
            combo.click_input()
            time.sleep(0.5)
            bring_to_foreground(combo)
            mouse_click(coords=(x, y))
            time.sleep(0.3)
            if combo.selected_text() == theme:
                return
        raise TimeoutError(f"Theme combo box still shows {combo.selected_text()!r} after {attempts} attempts to select {theme!r}")

    def close(self) -> None:
        find(self._window, AutomationID.SETTINGS_CLOSE_BUTTON).click_input()
