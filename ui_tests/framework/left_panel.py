"""Page objects for the left side panel (SidePanelWidget, Edge::Left) and its "Tree" tab --
FolderTreeView over FolderTreeModel (Architecture.md §14.41) -- and "Search" tab --
SearchPanelWidget (Architecture.md §14.42).
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


class SearchPanel:
    """The "Search" tab's content (SearchPanelWidget, Architecture.md §14.42): an async, name-only
    recursive search over the focused tab's current folder, rendered as a list (default) or,
    toggled via `view_as_tree_button`, a folder tree -- plus the relocated Advanced Search
    entry point.
    """

    def __init__(self, container: Callable[[], object]):
        self.search_bar = Element(container, AutomationID.SEARCH_PANEL_SEARCH_BAR)
        self.refresh_button = Element(container, AutomationID.SEARCH_REFRESH_BUTTON)
        self.clear_button = Element(container, AutomationID.SEARCH_CLEAR_BUTTON)
        self.view_as_tree_button = Element(container, AutomationID.SEARCH_VIEW_AS_TREE_BUTTON)
        self.collapse_all_button = Element(container, AutomationID.SEARCH_COLLAPSE_ALL_BUTTON)
        self.advanced_search_button = Element(container, AutomationID.ADVANCED_SEARCH_BUTTON)
        # Match Case/Whole Word/Exact Name/Match Regex toggles (Architecture.md §14.44).
        self.match_case_button = Element(container, AutomationID.SEARCH_MATCH_CASE_BUTTON)
        self.whole_word_button = Element(container, AutomationID.SEARCH_WHOLE_WORD_BUTTON)
        self.exact_name_button = Element(container, AutomationID.SEARCH_EXACT_NAME_BUTTON)
        self.regex_button = Element(container, AutomationID.SEARCH_REGEX_BUTTON)
        self._list_results = Element(container, AutomationID.SEARCH_LIST_RESULTS_VIEW)
        self._tree_results = Element(container, AutomationID.SEARCH_TREE_RESULTS_VIEW)

    def _list_results_container(self) -> Any:
        return self._list_results.raw

    def type_query(self, text: str) -> None:
        """Clicks the search bar and types `text` -- live-as-you-type, no Enter needed."""
        self.search_bar.click()
        self.search_bar.set_text(text)

    def click_refresh(self) -> None:
        self.refresh_button.click()

    def click_clear(self) -> None:
        self.clear_button.click()

    def is_tree_view(self) -> bool:
        return self.view_as_tree_button.is_checked()

    def toggle_view_as_tree(self) -> None:
        self.view_as_tree_button.click()

    def click_collapse_all(self) -> None:
        self.collapse_all_button.click()

    def click_advanced_search(self) -> None:
        self.advanced_search_button.click()

    def _tree_items(self) -> List[Any]:
        return [child for child in self._tree_results.raw.children() if child.element_info.control_type == "TreeItem"]

    def result_names(self) -> List[str]:
        """Display names of every currently shown result, top to bottom -- reads whichever of the
        list/tree pages is currently active (list is a Details-view FileBrowserView with every
        column but Name hidden; tree is a plain 1-column QTreeView). pywinauto/UIA exposes row
        text but not the delegate's bold/highlight styling, so "highlighted part" itself isn't
        assertable here -- only that the right names appear/are filtered.
        """
        if self.is_tree_view():
            return [item.window_text() for item in self._tree_items()]

        from .details_view import read_details_rows

        inner_tree_view = Element(self._list_results_container, AutomationID.FILE_TREE_VIEW)
        return [row.name for row in read_details_rows(inner_tree_view)]

    def double_click(self, name: str) -> None:
        """Double-clicks a result row by name, in whichever of list/tree mode is currently active
        -- navigates via SearchPanelWidget::resultActivated.
        """
        if self.is_tree_view():
            for item in self._tree_items():
                if item.window_text() == name:
                    item.double_click_input()
                    return
            raise LookupError(f"No search tree result named {name!r} among {self.result_names()}")

        inner_tree_view = Element(self._list_results_container, AutomationID.FILE_TREE_VIEW)
        for child in inner_tree_view.raw.children():
            if child.element_info.control_type == "TreeItem" and child.window_text() == name:
                child.double_click_input()
                return
        raise LookupError(f"No search result named {name!r} among {self.result_names()}")


class LeftPanel:
    """SidePanelWidget on the window's left edge. `.tree`/`.search` make sure the matching button
    is checked (panel expanded, that tab visible) before returning its page object -- same
    idempotent "only click when unchecked" posture as `RightPanel`.
    """

    def __init__(self, window_container: Callable[[], object]):
        self._panel = Element(window_container, AutomationID.LEFT_PANEL)
        self.tree_button = Element(window_container, AutomationID.LEFT_PANEL_TREE_BUTTON)
        self.search_button = Element(window_container, AutomationID.LEFT_PANEL_SEARCH_BUTTON)
        self._tree = FolderTree(self._container)
        self._search = SearchPanel(self._container)

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

    @property
    def search(self) -> SearchPanel:
        if not self.search_button.is_checked():
            self.search_button.click()
        return self._search
