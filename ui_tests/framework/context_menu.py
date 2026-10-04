"""Generic page object for a QMenu-based context menu -- `FileItemContextMenu::show`'s
per-selection item menu and `WorkspacePaneWidget::showBackgroundContextMenu`'s empty-space menu
(Architecture.md §14.13). Entries are plain `QAction`s with no persistent `AutomationID` (static
verbs like "Open"/"Cut"/"Copy" and the registry-sourced shell-extension entries alike) --
located and clicked by exact visible text.

Unlike the app's other QMenu-backed popups (`favorites_menu.py`'s menu bar dropdowns,
`tag_manager_dialog.py`'s Add/Rename buttons), a context menu opened via `QMenu::exec()` here
surfaces as a genuinely separate top-level window that (confirmed against a real build) reports
the *same* window title as the main window -- searching for it by `title="Exp-LORer"` through the
app's own `window` (what `favorites_menu.py` does) then raises pywinauto's
`ElementAmbiguousError` for as long as the popup is open, since the title now matches two
windows. `details_view.toggle_column_visibility`'s docstring flags the same ambiguity for the
header's column-visibility menu. The fix used here, like `modal_dialog.py`'s dialog handling, is
to connect directly by HWND -- detected as "whatever new top-level window just appeared" (see
`modal_dialog.wait_for_new_top_level_window`) -- rather than searching by title.
"""

from __future__ import annotations

from typing import Any, List

from .modal_dialog import connect_by_handle, visible_top_level_window_handles, wait_for_new_top_level_window


class ContextMenu:
    """A just-opened context-menu popup, connected directly by HWND (see module docstring)."""

    def __init__(self, popup: Any):
        self._popup = popup

    def _items(self) -> List[Any]:
        return [item for item in self._popup.descendants() if item.element_info.control_type == "MenuItem"]

    def item_texts(self) -> List[str]:
        """Every entry's visible text, in on-screen order -- includes disabled entries."""
        return [item.window_text() for item in self._items()]

    def click(self, text: str) -> None:
        """Clicks the entry with exact visible text `text`. Raises `LookupError` (listing what
        was actually there) if no such entry exists.
        """
        for item in self._items():
            if item.window_text() == text:
                item.click_input()
                return
        raise LookupError(f"No context-menu item named {text!r} among {self.item_texts()}")

    def close(self) -> None:
        """Dismisses the popup without choosing anything, via Escape."""
        from pywinauto.keyboard import send_keys

        send_keys("{ESC}")


def open_context_menu(element: Any, timeout: float = 5) -> ContextMenu:
    """Right-clicks `element` (a raw pywinauto wrapper -- e.g. `details_view.row_item(...)` for
    an item menu, or a view's own raw wrapper for an empty-space background menu) and returns a
    `ContextMenu` bound to the resulting popup once it has actually opened.
    """
    before = visible_top_level_window_handles()
    element.right_click_input()
    hwnd = wait_for_new_top_level_window(before, timeout=timeout)
    return ContextMenu(connect_by_handle(hwnd))
