"""Page object for `SettingsHotkeysPage` (SettingsHotkeysPage.cpp), reached via
`open_settings_dialog(app).select_category("Hotkeys")` -- General > Hotkeys, spec §4.

IMPORTANT environment limitation, confirmed by hand against a real build (not guessed): Qt's
`uia` accessibility bridge, for this `QTreeWidget`, exposes each row's own column-0 cell (the
action's display name) as a real `TreeItem` with real text, but the other three columns --
`HotkeyEditWidget` (the clickable shortcut button), the Clear/Reset `QToolButton` pair, and the
conflict `QLabel` -- are embedded via `setItemWidget` and come back as `TreeItem`s with EMPTY
text, no automation id of their own, and no separate `Button`/`Text` element anywhere in the
window's UI Automation tree at all (confirmed via a full `descendants()` dump of the whole Settings
dialog). This is a sharper version of the same limitation `details_view.py`'s `begin_rename`
docstring already documents for the Details-view inline rename editor -- apparently specific to
widgets embedded into a `QTreeWidget`/`QTreeView` cell via `setItemWidget`/`setIndexWidget`.

What DOES still work, confirmed by hand: each blank cell's reported `rectangle()` is still the
real on-screen position of the widget painted there, so a plain mouse click at that rectangle (or
a chosen fraction of it, for the two-button Clear/Reset cell) lands on the real embedded widget
and triggers it exactly as a user's click would -- `click_shortcut`/`clear`/`reset` below rely on
this. There is simply no way to *read back* the shortcut text or conflict-warning text through UI
Automation here; verify a chord/conflict by its functional effect (does the new chord actually
fire? does pressing Enter after Clear do nothing anymore?) or by reading `hotkeys.json` straight
off disk under the app's `--data-dir`, not through this page object.

A second, unrelated limitation: Qt's `uia` bridge for this tree only ever reports the rows
currently rendered in the viewport (confirmed by hand: with ~44 catalog rows across 6 categories,
only the first ~16 show up in `descendants()` right after opening the page) -- `_row_item` below
scrolls the tree (Home, then PageDown) until the requested row actually appears, rather than
assuming every row is always present.
"""

from __future__ import annotations

import time
from typing import Any, Dict, List, Tuple

from pywinauto.keyboard import send_keys
from pywinauto.mouse import click as mouse_click

from .automation_id import AutomationID
from .element import Element
from .focus import bring_to_foreground
from .modal_dialog import connect_by_handle, find_control, wait_for_window_handle

# SettingsHotkeysPage.cpp's categoryDisplayName() -- also HotkeyCatalogEntry::Category's exact six
# values, in spec §3's own subsection order. Used to tell a category header's TreeItem apart from
# an action row's (both are plain TreeItems once mixed together by `tree.descendants()`).
CATEGORY_NAMES: List[str] = [
    "Navigation",
    "Tabs",
    "Panes & Layout",
    "Panels",
    "Search & Tools",
    "File Operations & Selection",
]

# The buttons-column cell holds Clear then Reset, left to right (SettingsHotkeysPage.cpp's
# `buttonsLayout->addWidget(clearButton)` before `addWidget(resetButton)`) -- fractional x
# positions confirmed by hand to land reliably on each button without overlapping the other.
_CLEAR_BUTTON_FRACTION = 0.25
_RESET_BUTTON_FRACTION = 0.75
_SHORTCUT_CELL_FRACTION = 0.5


class SettingsHotkeysPage:
    def __init__(self, window: Any):
        self._window = window
        self.filter_edit = Element(lambda: window, AutomationID.SETTINGS_HOTKEYS_FILTER_EDIT)
        self.tree = Element(lambda: window, AutomationID.SETTINGS_HOTKEYS_TREE)
        self.restore_all_button = Element(lambda: window, AutomationID.SETTINGS_HOTKEYS_RESTORE_ALL_BUTTON)

    # -- filter box --

    def filter_actions(self, text: str) -> None:
        """Replaces the filter box's text -- narrows visible rows by action display name,
        case-insensitively (`SettingsHotkeysPage::applyFilter`). Uses `Element.set_text` (UIA's
        `SetValue`), safe here since the filter box is wired to `textChanged` (fires for both
        programmatic and typed edits), unlike some other search boxes in this app that need
        live-as-you-type `textEdited` and so require real keystrokes instead.
        """
        self.filter_edit.set_text(text)

    def clear_filter(self) -> None:
        self.filter_actions("")

    # -- row/category enumeration (only currently-rendered rows -- see module docstring) --

    def _tree_items(self) -> List[Any]:
        return list(self.tree.raw.descendants(control_type="TreeItem"))

    def visible_category_names(self) -> List[str]:
        """Every category header currently rendered, top to bottom (hidden once every one of its
        child rows is filtered out -- `SettingsHotkeysPage::applyFilter` -- or simply scrolled out
        of the viewport, see module docstring).
        """
        items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
        return [item.window_text() for item in items if item.window_text() in CATEGORY_NAMES]

    def visible_row_names(self) -> List[str]:
        """Every action row's display name currently rendered, top to bottom, across every
        category (see module docstring on viewport-only rendering).
        """
        items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
        return [item.window_text() for item in items if item.window_text() and item.window_text() not in CATEGORY_NAMES]

    def rows_by_category(self) -> Dict[str, List[str]]:
        """Every currently rendered action row's display name, grouped under its own category
        header's name, in on-screen order.
        """
        items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
        result: Dict[str, List[str]] = {}
        current = ""
        for item in items:
            text = item.window_text()
            if text in CATEGORY_NAMES:
                current = text
                result[current] = []
            elif current and text:
                result[current].append(text)
        return result

    def scroll_to_top(self) -> None:
        """Focuses the tree (via whatever action row is currently rendered first) and jumps back
        to the very first row -- call before re-reading `visible_row_names`/`rows_by_category`
        after a previous row lookup scrolled the tree elsewhere.
        """
        items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
        anchor = next((item for item in items if item.window_text() and item.window_text() not in CATEGORY_NAMES), None)
        if anchor is None:
            return
        anchor.click_input()
        send_keys("{HOME}")
        time.sleep(0.15)

    def rows_by_category_full(self, max_pages: int = 30) -> Dict[str, List[str]]:
        """Like `rows_by_category`, but scrolls the WHOLE tree from top to bottom first and
        accumulates every row seen along the way -- `rows_by_category`/`visible_row_names` only
        ever see whatever is currently rendered in the viewport (see module docstring), which is
        far fewer than the full ~44-row catalog. Leaves the tree scrolled to wherever it ends up;
        callers that need the top again should call `scroll_to_top()` afterwards. Intended for a
        one-time structural check, not for repeated use mid-test.

        Tracks "current category" ACROSS frames (not reset to unknown at the start of every
        frame): once a category's header has scrolled out of the viewport, a later frame may still
        render that category's trailing rows with no header above them at all, which a per-frame
        `rows_by_category()` call can't attribute to anything -- carrying the last-seen category
        forward is what lets those trailing rows still get counted correctly.

        Dedups by row text ALONE (every action's display name is unique catalog-wide, by design),
        not by `(category, text)` -- once the tree stops actually scrolling further (`{PGDN}`
        becomes a no-op at the bottom), the exact same frame keeps getting re-read, and its
        topmost rows (whichever category's tail is still on screen, now with that category's own
        header already scrolled off above the top) would otherwise get silently re-attributed to
        whatever LATER category's header this call happened to see last, double-counting them
        under the wrong category. Stops once two consecutive frames add nothing new, rather than
        always spending the full `max_pages` re-reading an already-stable bottom.
        """
        self.scroll_to_top()
        result: Dict[str, List[str]] = {name: [] for name in CATEGORY_NAMES}
        seen_rows: set[str] = set()
        current = ""
        stable_frames = 0
        for _ in range(max_pages):
            items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
            added_this_frame = False
            for item in items:
                text = item.window_text()
                if text in CATEGORY_NAMES:
                    current = text
                elif current and text and text not in seen_rows:
                    seen_rows.add(text)
                    result[current].append(text)
                    added_this_frame = True
            if added_this_frame:
                stable_frames = 0
            else:
                stable_frames += 1
                if stable_frames >= 2:
                    break
            send_keys("{PGDN}")
            time.sleep(0.15)
        return result

    # -- locating one row's own cells (see module docstring on why columns 1-3 are blank) --

    def _find_row_text(self, action_name: str) -> Any:
        for item in self._tree_items():
            if item.window_text() == action_name:
                return item
        return None

    def _row_item(self, action_name: str, timeout: float = 8) -> Any:
        found = self._find_row_text(action_name)
        if found is not None:
            return found

        # Not currently rendered -- scroll the tree into view (see module docstring) rather than
        # assuming every catalog row is always present in the UI Automation tree.
        items = sorted(self._tree_items(), key=lambda item: item.rectangle().top)
        anchor = next((item for item in items if item.window_text() and item.window_text() not in CATEGORY_NAMES), None)
        if anchor is None:
            raise LookupError(f"Hotkeys tree has no rendered rows to focus while looking for {action_name!r}")
        anchor.click_input()
        send_keys("{HOME}")
        time.sleep(0.15)

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            found = self._find_row_text(action_name)
            if found is not None:
                return found
            send_keys("{PGDN}")
            time.sleep(0.15)
        raise LookupError(f"No Hotkeys row named {action_name!r} found, even after scrolling the whole tree")

    def _row_cells(self, action_name: str, timeout: float = 8) -> Tuple[Any, Any, Any, Any]:
        """`(action_cell, shortcut_cell, buttons_cell, conflict_cell)` for one row, left to right
        -- only `action_cell` has real text (see module docstring); the other three are matched by
        sharing the same vertical (top/bottom) extent as `action_cell`.
        """
        action_cell = self._row_item(action_name, timeout=timeout)
        rect = action_cell.rectangle()
        others = [
            item
            for item in self._tree_items()
            if item.rectangle().top == rect.top and item.rectangle().bottom == rect.bottom and item.rectangle().left != rect.left
        ]
        others.sort(key=lambda item: item.rectangle().left)
        if len(others) != 3:
            raise LookupError(f"Expected 3 sibling cells for Hotkeys row {action_name!r}, found {len(others)}")
        return (action_cell, others[0], others[1], others[2])

    @staticmethod
    def _click_fraction(cell: Any, fraction: float) -> None:
        bring_to_foreground(cell)
        rect = cell.rectangle()
        x = rect.left + int((rect.right - rect.left) * fraction)
        y = (rect.top + rect.bottom) // 2
        mouse_click(coords=(x, y))

    # -- per-row operations --

    def click_shortcut(self, action_name: str) -> None:
        """Clicks the row's shortcut cell -- a real mouse click landing on the embedded
        `HotkeyEditWidget`, entering listening mode. Send the candidate chord (`send_chord`) or
        `{ESC}` next.
        """
        _, shortcut_cell, _, _ = self._row_cells(action_name)
        self._click_fraction(shortcut_cell, _SHORTCUT_CELL_FRACTION)

    @staticmethod
    def send_chord(keys: str) -> None:
        """Sends a pywinauto key-chord string (e.g. `"^+n"` for Ctrl+Shift+N, `"{F6}"` for a bare
        F6) to whatever currently has focus -- call right after `click_shortcut` put a row into
        listening mode.
        """
        send_keys(keys)

    def record_chord(self, action_name: str, keys: str) -> None:
        """`click_shortcut` + `send_chord` in one call, for the common case of a rebind expected
        to be accepted outright.
        """
        self.click_shortcut(action_name)
        time.sleep(0.2)
        send_keys(keys)
        time.sleep(0.3)

    def cancel_listening(self, action_name: str) -> None:
        """Clicks the row's shortcut cell then presses Escape -- cancels back to whatever was
        bound before, without changing anything.
        """
        self.click_shortcut(action_name)
        time.sleep(0.2)
        send_keys("{ESC}")
        time.sleep(0.2)

    def clear(self, action_name: str) -> None:
        """Clicks the row's Clear button (left half of the buttons cell) -- empties the binding."""
        _, _, buttons_cell, _ = self._row_cells(action_name)
        self._click_fraction(buttons_cell, _CLEAR_BUTTON_FRACTION)
        time.sleep(0.2)

    def reset(self, action_name: str) -> None:
        """Clicks the row's Reset button (right half of the buttons cell) -- restores the
        catalog default.
        """
        _, _, buttons_cell, _ = self._row_cells(action_name)
        self._click_fraction(buttons_cell, _RESET_BUTTON_FRACTION)
        time.sleep(0.2)

    # -- Restore All Defaults --

    def click_restore_all(self) -> None:
        self.restore_all_button.click()

    def confirm_restore_all(self, accept: bool = True, timeout: float = 5) -> None:
        """Answers the `QMessageBox::question` confirmation `click_restore_all` raises -- "Yes" if
        `accept`, else "No" (leaves every row untouched).
        """
        hwnd = wait_for_window_handle("Restore All Defaults", timeout=timeout)
        dialog = connect_by_handle(hwnd)
        find_control(dialog, "Button", "Yes" if accept else "No").click_input()
        time.sleep(0.2)
