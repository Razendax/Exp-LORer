"""Page objects for the 4 WorkspacePaneWidget instances (A-D) and what each one owns directly."""

from __future__ import annotations

from typing import Any, Callable, List

from .automation_id import AutomationID
from .bottom_panel import BottomPanel
from .element import Element
from .wait import wait_until


class PaneNavigation:
    """Toolbar + address bar shared by every WorkspacePaneWidget instance (PANE_* ids)."""

    def __init__(self, container: Callable[[], object]):
        self.back = Element(container, AutomationID.PANE_BACK_BUTTON)
        self.forward = Element(container, AutomationID.PANE_FORWARD_BUTTON)
        self.up = Element(container, AutomationID.PANE_UP_BUTTON)
        self.refresh = Element(container, AutomationID.PANE_REFRESH_BUTTON)
        self.view_mode = Element(container, AutomationID.PANE_VIEW_MODE_BUTTON)
        self.new_tab = Element(container, AutomationID.PANE_NEW_TAB_BUTTON)
        self.address_bar = Element(container, AutomationID.PANE_ADDRESS_BAR)

    def navigate_to(self, path: str, timeout: float = 5) -> None:
        """Types `path` into the address bar and presses Enter, then waits for the address bar to
        reflect it. TabViewModel::navigateTo dispatches its directory listing off the UI thread
        (Architecture.md's threading rule), so the address bar -- bound to currentPathChanged --
        doesn't update synchronously with the keypress; callers that need to know navigation has
        actually landed before reading the file list should wait on this rather than a bare sleep.

        Clicks the address bar first to give it keyboard focus before typing, like a real user
        would. Without it, `set_text` (UIA's ValuePattern.SetValue) writes the new path without
        the control ever gaining focus -- leaving a window, until the Enter keypress lands, where
        WorkspacePaneWidget's directory-watch-triggered refresh (see DirectoryWatchService) can
        fire and overwrite the address bar back to the previous path (e.g. right after that path
        was deleted, as happens between two ExpLorerTestCase methods sharing one instance --
        conftest's `_cleanup_tmp_path` removes the previous test's directory, which the still-bound
        tab is watching), permanently clobbering the navigation this call was trying to make.
        """
        self.address_bar.click()
        self.address_bar.set_text(path)
        self.address_bar.raw.type_keys("{ENTER}")
        wait_until(lambda: self.address_bar.text == path, timeout=timeout)


class PaneTabs:
    """The tab strip owned by one WorkspacePaneWidget (PANE_TAB_WIDGET id) -- lets a test read
    which tabs are open and switch between them. Qt's `uia` accessibility bridge surfaces each
    QTabBar tab as its own `TabItem` element somewhere under the QTabWidget (nested under an
    internal QTabBar, not necessarily a direct child) -- `descendants()` rather than `children()`
    finds them regardless of that nesting, in tab order.
    """

    def __init__(self, container: Callable[[], object]):
        self._tab_widget = Element(container, AutomationID.PANE_TAB_WIDGET)

    def _tab_items(self) -> List[Any]:
        return [
            item for item in self._tab_widget.raw.descendants() if item.element_info.control_type == "TabItem"
        ]

    def titles(self) -> List[str]:
        """Every open tab's title, in tab order -- the folder name
        WorkspacePaneWidget::tabLabelFor() derives from each tab's current path ("This PC" for the
        virtual root).
        """
        return [item.window_text() for item in self._tab_items()]

    def select(self, index: int) -> None:
        """Clicks the tab at `index` (0-based, tab order), making it the pane's active tab."""
        self._tab_items()[index].click_input()


class PaneStatusBar:
    def __init__(self, container: Callable[[], object]):
        self.counts_label = Element(container, AutomationID.PANE_STATUS_COUNTS_LABEL)
        self.quick_select_edit = Element(container, AutomationID.PANE_STATUS_QUICK_SELECT_EDIT)


class WorkspacePane:
    """One of the 4 WorkspacePaneWidget instances. Only the pane container id is per-instance;
    everything it owns (toolbar, address bar, tab content, status bar) reuses one fixed id across
    all 4 panes, so every nested lookup here is scoped through this pane's own container first.
    """

    def __init__(self, window_container: Callable[[], object], pane_automation_id: str):
        self._pane = Element(window_container, pane_automation_id)
        self.navigation = PaneNavigation(self._container)
        self.tabs = PaneTabs(self._container)
        self.status_bar = PaneStatusBar(self._container)
        self.file_browser = Element(self._container, AutomationID.FILE_BROWSER_VIEW)
        self.file_list_view = Element(self._container, AutomationID.FILE_LIST_VIEW)
        self.file_tree_view = Element(self._container, AutomationID.FILE_TREE_VIEW)
        self.bottom_panel = BottomPanel(self._container)

    def _container(self) -> Any:
        return self._pane.raw


class WorkspacePanes:
    """All 4 panes. Most tests run in the default single-pane layout and only ever touch `.a`
    (also reachable as `app.navigation`/`app.panes.a...` -- see `ExpLorerApp.navigation`);
    `.b`/`.c`/`.d` matter once a multi-pane layout (see `LayoutToolbar`) is active.
    """

    def __init__(self, window_container: Callable[[], object]):
        self.a = WorkspacePane(window_container, AutomationID.WORKSPACE_PANE_A)
        self.b = WorkspacePane(window_container, AutomationID.WORKSPACE_PANE_B)
        self.c = WorkspacePane(window_container, AutomationID.WORKSPACE_PANE_C)
        self.d = WorkspacePane(window_container, AutomationID.WORKSPACE_PANE_D)
