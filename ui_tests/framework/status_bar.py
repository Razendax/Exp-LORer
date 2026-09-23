"""Page object for MainStatusBarWidget (the main window's own status bar, not a pane's)."""

from __future__ import annotations

from typing import Callable

from .automation_id import AutomationID
from .element import Element


class MainStatusBar:
    def __init__(self, container: Callable[[], object]):
        self.drive_label = Element(container, AutomationID.MAIN_STATUS_DRIVE_LABEL)
        self.computer_name_label = Element(container, AutomationID.MAIN_STATUS_COMPUTER_NAME_LABEL)
