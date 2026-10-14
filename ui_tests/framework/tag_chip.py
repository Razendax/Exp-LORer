"""Page object for one tag chip (`TagChipWidget.cpp`) -- a colored pill with a name label and an
optional trailing '+'/'x' button, used by all three chip-bearing sections of TagPanelWidget and by
TagManagerDialog's selectable chips. Chips are fully dynamic/data-driven (a fresh QWidget per tag,
rebuilt wholesale on every `*Changed` signal), so -- per `automation_id.py`'s own convention for
such widgets -- they carry no fixed automation id and are located by their parent container plus
the chip's name text instead.
"""

from __future__ import annotations

import time
from typing import Any, Optional


def find_chip_label(container: Any, tag_name: str, timeout: float = 5) -> Any:
    """Returns the raw pywinauto wrapper for a tag chip's name label (a QLabel, TagChipWidget's
    `m_label`) whose text is exactly `tag_name`, searched among `container`'s descendants.
    `container` is a raw pywinauto element/wrapper (e.g. a page object's `.raw`, or a connected
    dialog window) -- anything with a `.descendants()` method.
    """
    deadline = time.monotonic() + timeout
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            for element in container.descendants():
                if element.element_info.class_name == "QLabel" and element.window_text() == tag_name:
                    return element
        except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors during teardown/rebuild
            last_error = exc
        time.sleep(0.15)
    raise LookupError(f"No tag chip named {tag_name!r} found in {container!r} ({last_error})")


class TagChip:
    """One located tag chip. Clicking the body (`.click()`) hits the chip's screen rect at the
    label -- the label has Qt::WA_TransparentForMouseEvents so a real click there is delivered to
    the owning TagChipWidget underneath, which is what actually emits `clicked`/handles
    selection/tag-search. `.click_add()`/`.click_remove()` hit the chip's own trailing '+'/'x'
    QToolButton (Addable/Removable kinds only).
    """

    def __init__(self, label: Any):
        self._label = label

    def click(self) -> None:
        self._label.click_input()

    def exists(self) -> bool:
        try:
            return bool(self._label.exists())
        except Exception:
            return False

    def click_add(self) -> None:
        self._click_trailing_button("+")

    def click_remove(self) -> None:
        self._click_trailing_button("x")

    def _click_trailing_button(self, text: str) -> None:
        chip_widget = self._label.parent()
        for child in chip_widget.children():
            if child.element_info.control_type == "Button" and child.window_text() == text:
                child.click_input()
                return
        raise LookupError(f"No {text!r} button on chip {self._label.window_text()!r}")


def find_chip(container: Any, tag_name: str, timeout: float = 5) -> TagChip:
    """Locates and wraps the tag chip named `tag_name` inside `container` (see `find_chip_label`)."""
    return TagChip(find_chip_label(container, tag_name, timeout=timeout))


def chip_count_text(container: Any, tag_name: str, timeout: float = 5) -> Optional[str]:
    """The count label text next to the `tag_name` chip (e.g. "3" or "3/7" for a nested tag with
    children -- see `TagChipWidget`'s `count`/`totalCount` constructor args), or `None` if that
    chip currently has no count label at all (Tag Manager's "#" show-counts toggle is off).
    """
    from .automation_id import find

    label = find_chip_label(container, tag_name, timeout=timeout)
    chip_widget = label.parent()
    try:
        count_label = find(chip_widget, "tagChipCountLabel", timeout=0.5)
    except LookupError:
        return None
    return count_label.window_text()


def chip_names(container: Any) -> list:
    """All tag chip names currently shown directly under `container` (a chip section's `.raw`),
    in display order -- e.g. to assert a section's exact chip set rather than probing one at a
    time.
    """
    return [
        element.window_text()
        for element in container.descendants()
        if element.element_info.class_name == "QLabel" and element.parent().element_info.class_name == "TagChipWidget"
    ]
