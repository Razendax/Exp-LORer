"""Generic pixel-position selection for a plain (non-editable) QComboBox's native popup list.

pywinauto's UIA `ComboBoxWrapper.select()` (by name or index) raises against this app's Qt combo
boxes -- confirmed by hand against a real build (see `settings_dialog.SettingsDialog.select_theme`,
the first page object that needed this): Qt's combo popup exposes no `Selection`/`SelectionItem`
pattern pywinauto's generic path can drive. Clicking the popup item's own on-screen position works
instead: the popup is a plain native top-level window, same width as the combo box, one row per
item in `items_order`, with its *currently selected* row (not always row 0) aligned to the combo
box's own rect.
"""

from __future__ import annotations

import time
from typing import Any, Sequence

from pywinauto.mouse import click as mouse_click

from .focus import bring_to_foreground


def select_combo_item(combo: Any, item_text: str, items_order: Sequence[str], attempts: int = 3) -> None:
    """Switches `combo` (a raw pywinauto wrapper for a closed, non-editable QComboBox) to
    `item_text`, given the combo's fixed display order `items_order`. See the module docstring
    for why this pixel-position approach is used instead of a name/index-based `select()` call.
    """
    if combo.selected_text() == item_text:
        return

    rect = combo.rectangle()
    row_height = rect.bottom - rect.top
    target_index = items_order.index(item_text)
    x = (rect.left + rect.right) // 2

    for _ in range(attempts):
        # The Combo box can lose OS foreground status between its container opening and this
        # call (e.g. the test runner's own window stealing it back) -- re-force it right before
        # each click, same posture as details_view.py's double-click helpers.
        bring_to_foreground(combo)
        combo.click_input()
        time.sleep(0.5)
        bring_to_foreground(combo)
        # The popup aligns its CURRENTLY SELECTED row with the combo box's own rect (see
        # SettingsDialog.select_theme) -- recomputed each attempt since a failed previous attempt
        # may have changed it.
        current_index = items_order.index(combo.selected_text())
        y = rect.top + (target_index - current_index) * row_height + row_height // 2
        mouse_click(coords=(x, y))
        time.sleep(0.3)
        if combo.selected_text() == item_text:
            return
    raise TimeoutError(f"Combo box still shows {combo.selected_text()!r} after {attempts} attempts to select {item_text!r}")
