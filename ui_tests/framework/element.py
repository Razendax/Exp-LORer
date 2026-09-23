"""`Element`: the one building block every page-object class in `framework` is made of.

Design choices, and why:

- Lazy lookup. `Element` stores a container-provider callable and an automation id suffix, not a
  pywinauto wrapper. Every method re-resolves via `find()` (see `automation_id.py`) at call time,
  so a reference stays valid across tab switches, dialog open/close, and other UI rebuilds that
  would otherwise leave a cached pywinauto object stale ("element not found"/COM errors).
- Fixed, explicit method surface (`click`, `text`, `set_text`, ...) instead of forwarding
  arbitrary attribute access to the underlying pywinauto wrapper. A `__getattr__` passthrough
  would technically "work" but IDEs can't autocomplete or type-check it -- every attribute access
  becomes a silent runtime gamble instead of something intellisense can confirm exists. `.raw`
  is the one deliberate, clearly-named escape hatch for anything not covered below.
"""

from __future__ import annotations

from typing import Any, Callable


class Element:
    """A single UI element, located by automation id suffix inside a container.

    `container` is a zero-arg callable returning the current pywinauto container to search
    (usually another Element's `.raw`, or the app's top-level window) -- a callable rather than
    a fixed reference so it can reflect a container that gets recreated (e.g. a dialog reopened).
    """

    def __init__(self, container: Callable[[], object], automation_id: str, timeout: float = 5):
        self._container = container
        self._automation_id = automation_id
        self._timeout = timeout

    @property
    def raw(self) -> Any:
        """The underlying pywinauto wrapper, freshly located. Use this for anything the explicit
        methods below don't cover -- it's the same object pywinauto/UI Automation gives you
        everywhere else, so any pywinauto API works on it.
        """
        from .automation_id import find

        return find(self._container(), self._automation_id, timeout=self._timeout)

    def exists(self, timeout: float = 0.5) -> bool:
        from .automation_id import find

        try:
            find(self._container(), self._automation_id, timeout=timeout)
            return True
        except LookupError:
            return False

    def click(self) -> None:
        self.raw.click_input()

    def double_click(self) -> None:
        self.raw.double_click_input()

    def right_click(self) -> None:
        self.raw.right_click_input()

    @property
    def text(self) -> str:
        return self.raw.window_text()

    def set_text(self, text: str) -> None:
        element = self.raw
        if hasattr(element, "set_edit_text"):
            element.set_edit_text(text)
        else:
            element.type_keys(text, with_spaces=True)

    def is_enabled(self) -> bool:
        return self.raw.is_enabled()

    def is_checked(self) -> bool:
        return bool(self.raw.get_toggle_state())

    def toggle(self) -> None:
        self.raw.toggle()
