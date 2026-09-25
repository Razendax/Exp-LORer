"""Page object for the Favorites menu (`MainWindow::createFavoritesMenu`, Architecture.md
§14.35) -- "Add to Favorites" (adds the focused tab's current selection, or its browsed folder
when nothing is selected) and "Manage Favorites..." (opens `ManageFavoritesDialog`, see
`manage_favorites_dialog.py`).

The per-favorite entries `rebuildFavoritesMenuItems` adds after those two aren't covered here:
each is a custom `QWidgetAction` row (icon + name + path, painted by hand -- see
`FavoriteMenuRowWidget` in `MainWindow.cpp`) rather than a plain `QAction`, and this repo has no
established pattern yet for driving that kind of widget-backed menu row via UI Automation. Assert
on favorites added this way through `ManageFavoritesDialog.entry_names()` instead, which reads
plain `QTreeWidgetItem` rows.

Like `open_tag_manager_dialog`, menu items are clicked through directly by title rather than via
pywinauto's `menu_select` -- see that function's docstring for why.
"""

from __future__ import annotations

from typing import Any

from .manage_favorites_dialog import ManageFavoritesDialog
from .modal_dialog import wait_for_window_handle


class FavoritesMenu:
    def __init__(self, app: Any):
        self._app = app

    def open(self) -> None:
        """Clicks the top-level "Favorites" menu, exposing its items for a subsequent click.
        Triggers `rebuildFavoritesMenuItems` (wired to the menu's `aboutToShow`), so the dynamic
        favorite entries are always current by the time this returns.
        """
        self._app.window.child_window(title="Favorites", control_type="MenuItem").click_input()

    def add_to_favorites_enabled(self) -> bool:
        """Opens the menu, reads whether "Add to Favorites" is enabled
        (`updateFavoritesActionEnabled`: disabled only while the focused tab is browsing the
        virtual This PC root), then closes the menu again without triggering anything.
        """
        self.open()
        enabled = self._app.window.child_window(title="Add to Favorites", control_type="MenuItem").is_enabled()
        self._app.window.type_keys("{ESC}")
        return enabled

    def add_to_favorites(self) -> None:
        """Opens the Favorites menu and clicks "Add to Favorites"."""
        self.open()
        self._app.window.child_window(title="Add to Favorites", control_type="MenuItem").click_input()

    def open_manage_favorites_dialog(self) -> ManageFavoritesDialog:
        """Opens the Favorites menu, clicks "Manage Favorites...", and returns a page object bound
        to the resulting dialog.
        """
        self.open()
        self._app.window.child_window(title="Manage Favorites...", control_type="MenuItem").click_input()
        hwnd = wait_for_window_handle("Manage Favorites")
        return ManageFavoritesDialog(hwnd)
