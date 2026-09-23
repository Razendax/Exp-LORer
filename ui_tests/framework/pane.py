"""Page objects for the 4 WorkspacePaneWidget instances (A-D) and what each one owns directly."""

from __future__ import annotations

from typing import Callable

from .automation_id import AutomationID
from .element import Element


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
        self.status_bar = PaneStatusBar(self._container)
        self.file_browser = Element(self._container, AutomationID.FILE_BROWSER_VIEW)
        self.file_list_view = Element(self._container, AutomationID.FILE_LIST_VIEW)
        self.file_tree_view = Element(self._container, AutomationID.FILE_TREE_VIEW)

    def _container(self):
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
