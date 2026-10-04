"""Minimal page object for the notification overlay (`NotificationOverlayWidget.cpp`,
Architecture.md §14.37) -- just enough to support `test_copy_move_operations.py`'s
progress-appears/disappears and completion-toast assertions (§14.47). A full Notification
Center page object (reading/dismissing multiple concurrent toasts individually, etc.) is Tier 3
work (§14.37) this doesn't attempt.

Toast and progress cards are dynamic -- any number can stack concurrently, each a fresh instance
sharing the same `AutomationID` -- so, unlike most page objects here, this one reads raw
`descendants()` under the one stable `NOTIFICATION_OVERLAY` anchor rather than resolving a
single `Element` for "the" card.
"""

from __future__ import annotations

from typing import Any, Callable, List

from .automation_id import AutomationID
from .element import Element


class NotificationOverlay:
    def __init__(self, container: Callable[[], object]):
        self._overlay = Element(container, AutomationID.NOTIFICATION_OVERLAY)

    def progress_visible(self) -> bool:
        """Whether any progress-notification card is currently showing."""
        try:
            return any(
                item.element_info.automation_id.endswith(AutomationID.PROGRESS_NOTIFICATION)
                for item in self._overlay.raw.descendants()
                if item.element_info.automation_id
            )
        except Exception:  # noqa: BLE001 -- overlay can be transiently unreachable mid-rebuild
            return False

    def cancel_progress(self) -> None:
        """Clicks the (single) progress card's Cancel button -- `ProgressNotificationWidget`'s
        cancel button has no `AutomationID` of its own, so it's located by control type/text,
        the same posture `modal_dialog.find_control` uses for native dialog buttons.
        """
        for item in self._overlay.raw.descendants():
            if item.element_info.control_type == "Button" and item.window_text() == "Cancel":
                item.click_input()
                return
        raise LookupError("No Cancel button on the current progress notification")

    def toast_texts(self) -> List[str]:
        """Every visible text fragment currently shown by toast card(s) -- title (e.g. "Info:
        Copy Complete") and body (e.g. "Items copied into ...") included, since neither has its
        own `AutomationID` (see `ToastNotificationWidget.cpp`). Check with `in`/`any(... in ...)`
        against this list rather than exact-matching one entry.
        """
        try:
            overlay = self._overlay.raw
        except Exception:  # noqa: BLE001 -- overlay can be transiently unreachable mid-rebuild
            return []
        return [item.window_text() for item in overlay.descendants() if item.element_info.control_type == "Text" and item.window_text()]

    def any_toast_contains(self, text: str) -> bool:
        return any(text in fragment for fragment in self.toast_texts())
