"""Page objects for the 4 WorkspacePaneWidget instances (A-D) and what each one owns directly."""

from __future__ import annotations

import time
from typing import Any, Callable, List, Tuple

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
        self._address_bar_popup = Element(container, AutomationID.PANE_ADDRESS_BAR_POPUP)

    def type_partial(self, text: str) -> None:
        """Clicks the address bar and types `text` via real keystrokes, without pressing Enter --
        for exercising the folder-suggestion popup (`AddressBarWidget`, Architecture.md §14.21),
        which is driven by `QLineEdit::textEdited` specifically. Like `SearchPanel.type_query`
        (see its docstring), `Element.set_text`'s UIA `SetValue` path only emits `textChanged`,
        never `textEdited`, so it would silently never show the popup.
        """
        self.address_bar.click()
        self.address_bar.raw.type_keys("^a{DELETE}" + text, with_spaces=True)

    def popup_suggestions(self) -> List[str]:
        """Folder names currently listed in the address bar's autocomplete popup
        (`PANE_ADDRESS_BAR_POPUP`, a `QListWidget`), top to bottom. Empty if the popup isn't
        showing.

        Confirmed against a real build: this is currently unable to see the popup's actual rows
        even while it's genuinely visible on screen. `AddressBarWidget` gives the popup
        `Qt::ToolTip | Qt::FramelessWindowHint` plus `WA_ShowWithoutActivating`, which makes it a
        separate native top-level, non-activating window rather than a normal child widget --
        reachable by automation id (unlike `modal_dialog.py`'s dialogs, this needs no HWND-diff
        trick, `find()` locates it fine among the main window's siblings), but it reports zero UI
        Automation descendants no matter how long the wait (ruled out the "brand-new top-level
        window populates its accessibility tree lazily" lag `modal_dialog.py` documents -- still
        empty after a 5s poll). Prefer asserting on the address bar's own inline-ghost-completion
        text (`address_bar.text`) and on where Enter/Tab/Down+Enter actually land instead of on
        this method's return value.
        """
        if not self._address_bar_popup.exists(timeout=0.5) or not self._address_bar_popup.raw.is_visible():
            return []
        return [
            item.window_text()
            for item in self._address_bar_popup.raw.children()
            if item.element_info.control_type == "ListItem"
        ]

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

    def is_enabled(self, index: int) -> bool:
        """Whether the tab at `index` can currently be switched to -- Media Viewer mode
        (Architecture.md §14.61) disables every other tab in the pane for its duration
        (`WorkspacePaneWidget::updateToolBarEnabledForMediaViewer`'s `setTabEnabled`), since its
        grid root is fixed to whichever tab entered it.
        """
        return bool(self._tab_items()[index].is_enabled())

    def item(self, index: int) -> Any:
        """The raw pywinauto `TabItem` wrapper at `index` -- for `.right_click_input()` (opening
        the "Move to Pane" context menu) or any other raw pywinauto API `titles`/`select`/
        `is_enabled` don't cover.
        """
        return self._tab_items()[index]

    def item_rect(self, index: int) -> Any:
        """The on-screen rectangle of the tab at `index` -- for computing drag source/target
        coordinates (see `drag_tab_to_reorder`/`drag_tab_to_other_pane` below).
        """
        return self.item(index).rectangle()


def drag_tab_to_reorder(tabs: "PaneTabs", from_index: int, to_index: int, steps: int = 12, step_delay: float = 0.05) -> None:
    """Drags the tab at `from_index` onto the tab currently at `to_index`'s position, reordering
    tabs within one pane's own strip (`QTabBar::setMovable(true)`, Architecture.md §14.109's
    same-pane reorder). Mirrors `details_view.drag_header`'s manual step-and-settle approach
    (small incremental moves, landing past the destination's midpoint, re-locating the
    destination on every step) for the same reason: a single canned jump doesn't feed
    `QTabBar`'s live drag-reorder tracking enough intermediate mouseMoveEvents to actually
    trigger a swap, and the destination's own position can shift mid-drag as tabs swap places.
    """
    source = tabs.item(from_index)
    src_rect = source.rectangle()
    y = (src_rect.top + src_rect.bottom) // 2
    x_start = (src_rect.left + src_rect.right) // 2

    def target_x() -> int:
        dest_rect = tabs.item_rect(to_index)
        return dest_rect.left + int((dest_rect.right - dest_rect.left) * 0.8)

    source.press_mouse_input(coords=(x_start, y))
    time.sleep(0.2)
    for i in range(1, steps + 1):
        x = x_start + (target_x() - x_start) * i // steps
        source.move_mouse_input(coords=(x, y))
        time.sleep(step_delay)
    time.sleep(0.2)
    source.release_mouse_input(coords=(target_x(), y))


def drag_tab_to_other_pane(
    source_tabs: "PaneTabs",
    source_index: int,
    target_point: Callable[[], Tuple[int, int]],
    steps: int = 20,
    step_delay: float = 0.05,
) -> None:
    """Drags the tab at `source_index` in `source_tabs` out of its own pane and releases it at
    `target_point()` -- either another pane's tab strip (position-aware insertion, see
    `PaneTabBar::insertionIndexForDrop`) or its content area (always appended last, see
    `WorkspacePaneWidget::dropEvent`), depending only on where `target_point` lands.

    Unlike `drag_tab_to_reorder`'s `target_x()` (re-evaluated every step -- an in-process,
    Qt-internal drag that keeps the whole app's UI Automation tree responsive throughout),
    `target_point` is resolved exactly once, *before* the drag starts. Confirmed against a real
    build: the moment this drag leaves the source bar's own rect, `PaneTabBar` hands off into a
    real OS-level `QDrag::exec()` (Architecture.md §14.109's `mouseMoveEvent` hand-off), and the
    app's UI Automation provider stops answering queries from this process for as long as that
    native drag loop is running -- re-querying a destination element's position mid-drag (as a
    per-step callable would) hangs until `wait_until`'s default timeout and then raises, which
    previously left the mouse button physically held down (the exception skipped the eventual
    `release_mouse_input` entirely), corrupting every later interaction in the whole test run.
    The `try`/`finally` below guarantees a release at the resolved target even if some other step
    still raises.
    """
    source = source_tabs.item(source_index)
    rect = source.rectangle()
    x0, y0 = (rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2
    tx, ty = target_point()
    exit_y = rect.bottom + 40

    source.press_mouse_input(coords=(x0, y0))
    try:
        time.sleep(0.2)
        source.move_mouse_input(coords=(x0, exit_y))
        time.sleep(0.2)

        for i in range(1, steps + 1):
            x = x0 + (tx - x0) * i // steps
            y = exit_y + (ty - exit_y) * i // steps
            source.move_mouse_input(coords=(x, y))
            time.sleep(step_delay)
        time.sleep(0.2)
    finally:
        source.release_mouse_input(coords=(tx, ty))


class PaneStatusBar:
    def __init__(self, container: Callable[[], object]):
        self.counts_label = Element(container, AutomationID.PANE_STATUS_COUNTS_LABEL)
        self.quick_select_edit = Element(container, AutomationID.PANE_STATUS_QUICK_SELECT_EDIT)


class AdvancedSearchPanel:
    """The Advanced (criteria) Search pane (`SearchCriteriaPanel` + its results, wrapped together
    as `SearchResultsPane`, Architecture.md §14.19) -- one of this pane's current tab's two
    `QStackedWidget` pages (page 0 is plain browsing, page 1 is this), swapped in by
    `SearchPanelWidget`'s "Advanced Search..." button (`LeftPanel.search.click_advanced_search()`)
    or by clicking a tag chip (§14.20). Scoped through the *pane's* own container, not the left
    panel's -- despite the entry-point button living in the left panel's Search tab, the pane
    itself (its own per-tab content) is where the criteria fields and results actually render.

    Results render in the same `FILE_TREE_VIEW`/`FILE_LIST_VIEW` ids as normal browsing (reused
    per the per-tab-content id convention, automation_id.py); read them with `result_names()`
    below rather than `details_view.read_details_rows` -- confirmed against a real build that at
    this app's default window size, every one of `FileListModel::ColumnCount` (8) columns is
    visible but the results tree itself is narrower than their combined width, so trailing
    columns (Date created/Percent) are scrolled out of the viewport and contribute no cell at
    all, breaking `read_details_rows`' fixed 8-cells-per-row assumption.
    """

    # Fixed display order of SearchCriteriaPanel's size unit combos (addItems() call in the
    # constructor) -- used by set_min_size/set_max_size's `unit` argument via select_combo_item.
    _SIZE_UNIT_ORDER = ["KB", "MB", "GB"]

    def __init__(self, container: Callable[[], object]):
        self._panel = Element(container, AutomationID.SEARCH_CRITERIA_PANEL)
        self.name_edit = Element(container, AutomationID.SEARCH_NAME_EDIT)
        self.min_size_spin = Element(container, AutomationID.SEARCH_MIN_SIZE_SPIN)
        self.min_size_unit_combo = Element(container, AutomationID.SEARCH_MIN_SIZE_UNIT_COMBO)
        self.max_size_spin = Element(container, AutomationID.SEARCH_MAX_SIZE_SPIN)
        self.max_size_unit_combo = Element(container, AutomationID.SEARCH_MAX_SIZE_UNIT_COMBO)
        self.submit_button = Element(container, AutomationID.SEARCH_SUBMIT_BUTTON)
        self.close_button = Element(container, AutomationID.ADVANCED_SEARCH_CLOSE_BUTTON)
        # Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles (Architecture.md
        # §14.56) -- same ExclusiveOptional behavior as `LeftPanel.search`'s equivalent 5 buttons.
        self.match_case_button = Element(container, AutomationID.ADVANCED_SEARCH_MATCH_CASE_BUTTON)
        self.whole_word_button = Element(container, AutomationID.ADVANCED_SEARCH_WHOLE_WORD_BUTTON)
        self.exact_name_button = Element(container, AutomationID.ADVANCED_SEARCH_EXACT_NAME_BUTTON)
        self.regex_button = Element(container, AutomationID.ADVANCED_SEARCH_REGEX_BUTTON)
        self.skip_git_ignored_button = Element(container, AutomationID.ADVANCED_SEARCH_SKIP_GIT_IGNORED_BUTTON)
        self.tag_edit = Element(container, AutomationID.SEARCH_TAG_EDIT)
        self.results_pane = Element(container, AutomationID.ADVANCED_SEARCH_RESULTS_PANE)
        self._results_tree_view = Element(lambda: self.results_pane.raw, AutomationID.FILE_TREE_VIEW)

    def is_visible(self) -> bool:
        """Whether this pane's per-tab stack is currently showing the Advanced Search page
        (vs. plain browsing), without triggering it.
        """
        try:
            return self._panel.exists(timeout=0.5) and bool(self._panel.raw.is_visible())
        except LookupError:
            return False

    def set_name(self, text: str) -> None:
        """Replaces the Name field's text -- selects everything and deletes it first (rather than
        relying on typing-over-a-selection to replace it) so clearing the field (`text=""`)
        actually works too.
        """
        self.name_edit.click()
        self.name_edit.raw.type_keys("^a{DELETE}" + text, with_spaces=True)

    def set_min_size_kb(self, value: int) -> None:
        self.min_size_spin.click()
        self.min_size_spin.raw.type_keys("^a{DELETE}" + str(value))

    def set_max_size_kb(self, value: int) -> None:
        self.max_size_spin.click()
        self.max_size_spin.raw.type_keys("^a{DELETE}" + str(value))

    def set_min_size(self, value: int, unit: str = "KB") -> None:
        """Like `set_min_size_kb`, but also switches the Min size unit combo (Architecture.md
        §14.56) to `unit` ("KB"/"MB"/"GB") first when it isn't already KB -- the combo is
        sticky UI-only state, so a fresh panel always starts on KB.
        """
        if unit != "KB":
            from .combo_box import select_combo_item

            select_combo_item(self.min_size_unit_combo.raw, unit, self._SIZE_UNIT_ORDER)
        self.set_min_size_kb(value)

    def set_max_size(self, value: int, unit: str = "KB") -> None:
        """Like `set_max_size_kb`, but also switches the Max size unit combo to `unit` first --
        see `set_min_size`.
        """
        if unit != "KB":
            from .combo_box import select_combo_item

            select_combo_item(self.max_size_unit_combo.raw, unit, self._SIZE_UNIT_ORDER)
        self.set_max_size_kb(value)

    def submit(self) -> None:
        self.submit_button.click()

    def is_searching(self) -> bool:
        """Whether the Search button currently reads "Searching..." (`setSearching(true)`,
        Architecture.md §14.38) rather than its idle "Search" label.
        """
        return self.submit_button.text != "Search"

    def close(self) -> None:
        self.close_button.click()

    def result_names(self) -> List[str]:
        """Display names of every currently shown result row, top to bottom. Reads only the Name
        column (`details_view.read_name_column`) rather than the full 8-column
        `read_details_rows` shape -- confirmed against a real build that at this app's default
        window size, the results tree is narrower than its own 8 columns' combined width, so
        trailing columns (Date created/Percent) can be scrolled out of view and contribute no
        cell at all, which would otherwise misalign a fixed-stride row reader.
        """
        from .details_view import read_name_column

        return read_name_column(self._results_tree_view)


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
        self.advanced_search = AdvancedSearchPanel(self._container)

    def _container(self) -> Any:
        return self._pane.raw

    def media_viewer_result_names(self) -> List[str]:
        """Display names currently shown in Media Viewer mode's locked icon-grid (`FileBrowserView::
        DisplayMode::MediaViewerGrid`, Architecture.md §14.61) -- reuses `file_list_view` (the same
        fixed id every display mode's `QListView` shares), top to bottom/left to right.
        """
        from .details_view import read_grid_item_names

        return read_grid_item_names(self.file_list_view)


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
