"""Page object for the main window's pane-layout toolbar (single/split/grid)."""

from __future__ import annotations

from typing import Callable

from .automation_id import AutomationID
from .element import Element


class LayoutToolbar:
    def __init__(self, container: Callable[[], object]):
        self.single = Element(container, AutomationID.LAYOUT_SINGLE_BUTTON)
        self.two_vertical = Element(container, AutomationID.LAYOUT_TWO_VERTICAL_BUTTON)
        self.two_horizontal = Element(container, AutomationID.LAYOUT_TWO_HORIZONTAL_BUTTON)
        self.four_grid = Element(container, AutomationID.LAYOUT_FOUR_GRID_BUTTON)
