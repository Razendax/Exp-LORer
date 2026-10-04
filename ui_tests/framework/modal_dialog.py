"""Helpers for dynamically-spawned top-level Qt dialogs that have no persistent automation id of
their own -- TagManagerDialog (opened from Edit > "Tag Edit...", a QAction-only menu entry with no
backing widget) and the QInputDialog/QMessageBox popups it in turn opens for Add/Rename/Delete.

Why this exists: such a dialog's HWND is a genuine native top-level window (confirmed via raw
Win32 `EnumWindows`) the instant Qt shows it, but pywinauto's UIA-backend searches
(`Application.window(title=...)`, `Desktop().windows()`, `child_window(...).click_input()`) can
spuriously miss it -- and miss its own children for a further short while after that -- for up to
a second or so right after creation. Apparently Qt's UI Automation accessibility bridge populates
a *brand new* top-level window's provider/tree lazily rather than the instant the HWND exists, so
a title/tree-based pywinauto search can run before that population has caught up. Connecting
directly by HWND (`Application(backend='uia').connect(handle=hwnd)`) rather than by title, and
polling for descendants with a manual retry loop (mirroring `automation_id.find()`'s own proven
pattern) rather than pywinauto's built-in `child_window()` resolution, reliably rides out that
window.
"""

from __future__ import annotations

import time
from typing import Any, List, Optional

import win32gui
from pywinauto.application import Application

from .wait import wait_until


def visible_top_level_window_handles() -> List[int]:
    """A snapshot of every currently visible top-level window handle (raw Win32 enumeration, not
    UI Automation) -- pair with `wait_for_new_top_level_window` to detect a window that just
    appeared (e.g. an externally-launched default-app window opened via "Open with default
    application") without depending on a known title or process name.
    """
    handles: List[int] = []
    win32gui.EnumWindows(
        lambda hwnd, _: handles.append(hwnd) if win32gui.IsWindowVisible(hwnd) else None, None
    )
    return handles


def wait_for_new_top_level_window(before: List[int], timeout: float = 5) -> int:
    """Polls for a visible top-level window handle not present in `before` (a prior
    `visible_top_level_window_handles()` snapshot), returning its HWND once found. Raises on
    timeout via `wait_until`.
    """

    def _find() -> Optional[int]:
        new = [hwnd for hwnd in visible_top_level_window_handles() if hwnd not in before]
        return new[0] if new else None

    return wait_until(_find, timeout=timeout)


def wait_for_window_handle(title: str, timeout: float = 5) -> int:
    """Polls raw Win32 window enumeration (not UI Automation) for a visible top-level window
    titled exactly `title`, returning its HWND once found.
    """

    def _find() -> Optional[int]:
        hwnd = win32gui.FindWindow(None, title)
        return hwnd if hwnd and win32gui.IsWindowVisible(hwnd) else None

    return wait_until(_find, timeout=timeout)


def wait_for_second_window_handle(title: str, exclude_hwnd: int, timeout: float = 5) -> int:
    """Like `wait_for_window_handle`, but for when two windows can carry the same title at once
    -- TagManagerDialog and the `QMessageBox::warning(this, tr("Tag Manager"), ...)` it shows on a
    failed operation are both titled "Tag Manager". Returns the first matching handle that isn't
    `exclude_hwnd` (the already-known dialog).
    """

    def _find() -> Optional[int]:
        found = []

        def _enum(hwnd: int, _: None) -> None:
            if hwnd != exclude_hwnd and win32gui.GetWindowText(hwnd) == title and win32gui.IsWindowVisible(hwnd):
                found.append(hwnd)

        win32gui.EnumWindows(_enum, None)
        return found[0] if found else None

    return wait_until(_find, timeout=timeout)


def connect_by_handle(hwnd: int) -> Any:
    """Returns a pywinauto window bound directly to `hwnd`, rather than found by title/tree
    search (see module docstring for why that distinction matters here).
    """
    return Application(backend="uia").connect(handle=hwnd).window(handle=hwnd)


def find_control(container: Any, control_type: str, text: Optional[str] = None, timeout: float = 5) -> Any:
    """Locates a plain Qt-dialog control (QInputDialog's line edit, QMessageBox/QInputDialog's
    OK/Cancel/Yes/No buttons -- none of which carry a custom automation id) by control type and
    optionally exact visible text, retrying to ride out the population lag described in the
    module docstring.
    """
    deadline = time.monotonic() + timeout
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            for element in container.descendants():
                if element.element_info.control_type == control_type and (text is None or element.window_text() == text):
                    return element
        except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors during teardown/rebuild
            last_error = exc
        time.sleep(0.15)
    raise LookupError(f"No {control_type!r} control (text={text!r}) found in {container!r} ({last_error})")
