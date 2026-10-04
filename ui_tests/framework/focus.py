"""Helpers for making mouse-driven activation (double-click) and window-activation-dependent
actions land reliably, confirmed by hand against a real build to otherwise be flaky/non-functional
in some environments.

- `bring_to_foreground`: forces a window to the OS foreground immediately before an action whose
  registration depends on genuine input focus/activation state -- double-clicks and Enter-based
  `QAbstractItemView::activated` -- as opposed to a plain synthesized click, which registers (via
  the SelectionItem pattern) regardless of foreground state. Confirmed by hand: forcing
  foreground once at app startup (`ExpLorerApp.start`) is NOT enough on its own -- some other
  interaction in between (typing into a line edit, opening a menu, ...) can cost the window its
  foreground status again well before a later double-click/Enter actually needs it. Call this
  right before the double-click/Enter itself, not just once per test.
- `double_click_at`: a raw-Win32 double-click, for the same `QAbstractItemView::activated` actions.
  Confirmed by hand that `pywinauto.mouse.double_click` can be unreliable here even with the
  window already foregrounded -- each of its two synthesized clicks apparently involves its own
  cursor-positioning step, and any resulting sub-pixel jitter between them is enough to fall
  outside Windows' own double-click position tolerance, so the OS never recognizes the pair as one
  double-click. Positioning the cursor once and then sending two bare button-down/up event pairs
  with a short, explicit gap (comfortably inside `SystemInformation.DoubleClickTime`) avoids that.
"""

from __future__ import annotations

from typing import Any


def bring_to_foreground(element: Any) -> None:
    """Best-effort: brings `element`'s top-level window to the OS foreground. `element` is a raw
    pywinauto wrapper (e.g. `some_element.raw`), not an `Element`.
    """
    import win32gui

    try:
        win32gui.SetForegroundWindow(element.top_level_parent().handle)
    except Exception:  # noqa: BLE001 -- best-effort; a failure here shouldn't abort the action
        pass


def double_click_at(x: int, y: int) -> None:
    """Double-clicks at the given screen coordinates via raw `win32api` mouse events -- see the
    module docstring for why this is used instead of `pywinauto.mouse.double_click`.
    """
    import time

    import win32api
    import win32con

    win32api.SetCursorPos((x, y))
    time.sleep(0.02)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(0.03)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
