"""Page objects for the left side panel (SidePanelWidget, Edge::Left) and its "Tree" tab --
FolderTreeView over FolderTreeModel (Architecture.md §14.41).
"""

from __future__ import annotations

import time
from typing import Any, Callable, List, Optional

from .automation_id import AutomationID
from .element import Element


class FolderTree:
    """The folders-only tree. Qt's `uia` bridge exposes every *visible* (expanded-into) row of a
    QTreeView as a flat `TreeItem` child of the view, in display order -- the same shape
    `details_view.py` reads the main view's rows from -- so a folder only shows up here once its
    ancestors are expanded.
    """

    def __init__(self, container: Callable[[], object]):
        self.view = Element(container, AutomationID.FOLDER_TREE_VIEW)

    def _items(self) -> List[Any]:
        return [child for child in self.view.raw.children() if child.element_info.control_type == "TreeItem"]

    def visible_names(self) -> List[str]:
        """Display names of every currently visible row, top to bottom."""
        return [item.window_text() for item in self._items()]

    def selected_name(self) -> Optional[str]:
        """Display name of the selected row (the tree follows the active tab's folder), or None."""
        for item in self._items():
            try:
                if item.is_selected():
                    return item.window_text()
            except Exception:  # noqa: BLE001 -- row without SelectionItem pattern
                continue
        return None

    def item(self, name: str, timeout: float = 5) -> Any:
        """The visible row named `name`. Raises LookupError if none shows up within `timeout`."""
        deadline = time.monotonic() + timeout
        last_error: Optional[BaseException] = None
        while time.monotonic() < deadline:
            try:
                for item in self._items():
                    if item.window_text() == name:
                        return item
            except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
                last_error = exc
            time.sleep(0.15)
        raise LookupError(f"No folder-tree row named {name!r} ({last_error})")

    def click(self, name: str) -> None:
        """Single-clicks a row's text -- navigates the focused tab there."""
        self.item(name).click_input()

    def rename(self, name: str, new_name: str) -> None:
        """Selects `name`, presses F2, types `new_name` and commits with Enter -- the tree's
        inline FileNameEditDelegate rename. Keys go to whatever has focus (the freshly opened
        editor), not re-targeted at the view, which would close the editor.
        """
        from pywinauto.keyboard import send_keys

        self.click(name)
        send_keys("{F2}")
        time.sleep(0.3)
        send_keys("^a" + new_name + "{ENTER}", with_spaces=True)


class LeftPanel:
    """SidePanelWidget on the window's left edge. `.tree` makes sure the "Tree" button is checked
    (panel expanded, tree visible) before returning the tree's page object -- same idempotent
    "only click when unchecked" posture as `RightPanel`.
    """

    def __init__(self, window_container: Callable[[], object]):
        self._panel = Element(window_container, AutomationID.LEFT_PANEL)
        self.tree_button = Element(window_container, AutomationID.LEFT_PANEL_TREE_BUTTON)
        self._tree = FolderTree(self._container)

    def _container(self) -> Any:
        return self._panel.raw

    def is_expanded(self) -> bool:
        return self.tree_button.is_checked()

    def tree_visible(self) -> bool:
        """Whether the tree is currently on screen (panel expanded), without expanding it."""
        try:
            return self._tree.view.exists(timeout=0.5) and bool(self._tree.view.raw.is_visible())
        except LookupError:
            return False

    @property
    def tree(self) -> FolderTree:
        if not self.tree_button.is_checked():
            self.tree_button.click()
        return self._tree
