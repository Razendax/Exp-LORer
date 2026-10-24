"""Page objects for `BottomPanelWidget`'s per-tab panel tabs (Terminal / Folder Size,
Architecture.md's disk-usage feature) -- one instance per open tab, reusing the fixed ids every
tab's content keeps (see automation_id.py's module docstring).

Only the "Folder Size" tab is covered here: its pie chart is deliberately not exposed (hand-painted
QPainter widget with no per-segment automation surface -- not covered by this framework, and
explicitly out of scope for automated coverage), and "Scan Drive" is exposed only as a plain
`Element` (no `scan_drive()` convenience method) since it recursively scans the whole drive and
isn't meant to be exercised by this test suite.
"""

from __future__ import annotations

from typing import Any, Callable

from .automation_id import AutomationID
from .element import Element
from .wait import wait_until


class FolderSizePanel:
    def __init__(self, container: Callable[[], object]):
        self.scan_folder_button = Element(container, AutomationID.FOLDER_SIZE_SCAN_FOLDER_BUTTON)
        self.scan_drive_button = Element(container, AutomationID.FOLDER_SIZE_SCAN_DRIVE_BUTTON)

    def scan_folder(self, timeout: float = 20) -> None:
        """Clicks "Scan Folder" and waits for the scan to finish -- FolderSizeViewModel::scanFolder
        recursively walks the tab's current folder on a background thread and persists the result;
        the button's text flips back from "Scanning..." to "Scan Folder" (FolderSizeWidget::
        onScanningChanged) once that completes.

        Tries to first observe the button showing "Scanning..." -- otherwise the completion check
        below could pass immediately on a pre-click "Scan Folder" read, before the cross-process
        UI Automation click has even reached the app, silently skipping the wait for an actual
        scan. But that observation is only best-effort (a short, separate timeout, swallowed
        rather than propagated): for a handful of tiny test files, the scan -- worker-thread hop
        included -- can finish faster than even one poll from this process, so "Scanning..." isn't
        guaranteed to be *catchable*, only to have genuinely happened. The completion wait's own
        timeout is what actually bounds this method.
        """
        self.scan_folder_button.click()
        try:
            wait_until(lambda: self.scan_folder_button.text == "Scanning...", timeout=0.5, interval=0.02)
        except TimeoutError:
            pass
        wait_until(lambda: self.scan_folder_button.text == "Scan Folder", timeout=timeout)


class BottomPanel:
    """`BottomPanelWidget` itself -- starts collapsed; `.folder_size` expands it to the "Folder
    Size" tab (clicking its checkable panel-tab button) the same way `RightPanel`'s properties
    switch/expand their target page, and is safe to access repeatedly (only clicks when not
    already the active/checked tab).
    """

    def __init__(self, container: Callable[[], object]):
        self._container = container
        self._terminal_button = Element(container, AutomationID.BOTTOM_PANEL_TERMINAL_BUTTON)
        self._folder_size_button = Element(container, AutomationID.BOTTOM_PANEL_FOLDER_SIZE_BUTTON)
        self._folder_size = FolderSizePanel(container)

    @property
    def folder_size(self) -> FolderSizePanel:
        if not self._folder_size_button.is_checked():
            self._folder_size_button.click()
        return self._folder_size
