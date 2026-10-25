"""Coverage for Tab Drag-to-Reorder, Drag-to-Move, and "Move to Pane" Context Menu
(Architecture.md §14.109): dragging a tab within its own pane's strip to reorder it, dragging a
tab onto a different currently-visible pane to move it there (either its tab strip, position-
aware, or its content area, always appended last), and the non-drag "Move to Pane" context-menu
alternative for the same cross-pane move.

Every tab's target folder is created empty under `tmp_path` so a cross-pane content-area drop
lands on blank space in the destination pane's file browser view, rather than risking the
file/tree view's own internal (file-move) drag-and-drop handling intercepting the drop before it
reaches `WorkspacePaneWidget::dropEvent`.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Tuple

import pytest

from framework import (
    ExpLorerConfig,
    ExpLorerConfiguredFreshAppTestCase,
    ExpLorerFreshAppTestCase,
    SplitLayout,
    open_context_menu,
    wait_until,
)
from framework.modal_dialog import visible_top_level_window_handles, wait_for_new_top_level_window
from framework.pane import WorkspacePane, drag_tab_to_other_pane, drag_tab_to_reorder


def _tab_strip_point(pane: WorkspacePane, index: int, bias: float) -> Callable[[], Tuple[int, int]]:
    """Point-getter landing inside `pane`'s tab at `index`, `bias` of the way across its width
    (0.1 = well left of its midpoint -> inserts before it; 0.9 = well right -> inserts after it,
    per `PaneTabBar::insertionIndexForDrop`).
    """

    def _point() -> Tuple[int, int]:
        rect = pane.tabs.item_rect(index)
        x = rect.left + int((rect.right - rect.left) * bias)
        y = (rect.top + rect.bottom) // 2
        return (x, y)

    return _point


def _content_area_point(pane: WorkspacePane) -> Callable[[], Tuple[int, int]]:
    """Point-getter landing in the middle of `pane`'s (empty) file browser view -- a content-area
    drop, always appended last regardless of x/y.
    """

    def _point() -> Tuple[int, int]:
        rect = pane.file_browser.raw.rectangle()
        return ((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)

    return _point


class TestSamePaneTabReorder(ExpLorerFreshAppTestCase):
    """Drag-to-reorder within one pane's own tab strip (`QTabBar::setMovable(true)`)."""

    def test_dragging_a_tab_onto_an_adjacent_tab_swaps_their_order(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        third = tmp_path / "third"
        for folder in (first, second, third):
            folder.mkdir()

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(second))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(third))
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["first", "second", "third"])

        drag_tab_to_reorder(self.app.panes.a.tabs, from_index=0, to_index=1)

        wait_until(lambda: self.app.panes.a.tabs.titles() == ["second", "first", "third"])

    def test_dragging_the_last_tab_onto_the_first_moves_it_to_the_front(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(second))
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["first", "second"])

        drag_tab_to_reorder(self.app.panes.a.tabs, from_index=1, to_index=0)

        wait_until(lambda: self.app.panes.a.tabs.titles() == ["second", "first"])


class TestCrossPaneTabDrag(ExpLorerConfiguredFreshAppTestCase):
    """Drag-to-move a tab onto a different, currently-visible pane -- both the position-aware
    tab-strip drop and the always-appended-last content-area drop.
    """

    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig, tmp_path: Path) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        a1 = tmp_path / "a1"
        a2 = tmp_path / "a2"
        b1 = tmp_path / "b1"
        b2 = tmp_path / "b2"
        for folder in (a1, a2, b1, b2):
            folder.mkdir()
        config.workspace.panes.a.add_tab(path=str(a1)).add_tab(path=str(a2))
        config.workspace.panes.b.add_tab(path=str(b1)).add_tab(path=str(b2))

    def test_dragging_a_tab_onto_another_panes_content_area_appends_it_last(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a1", "a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1", "b2"])

        drag_tab_to_other_pane(
            self.app.panes.a.tabs, source_index=0, target_point=_content_area_point(self.app.panes.b)
        )

        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1", "b2", "a1"])

    def test_dragging_a_tab_onto_another_panes_first_tab_left_half_inserts_it_before(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a1", "a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1", "b2"])

        drag_tab_to_other_pane(
            self.app.panes.a.tabs, source_index=0, target_point=_tab_strip_point(self.app.panes.b, index=0, bias=0.1)
        )

        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["a1", "b1", "b2"])

    def test_dragging_a_tab_moves_focus_to_the_destination_pane(self) -> None:
        drag_tab_to_other_pane(
            self.app.panes.a.tabs, source_index=0, target_point=_content_area_point(self.app.panes.b)
        )

        wait_until(lambda: self.app.panes.b.tabs.titles()[-1] == "a1")
        # The moved tab becomes the destination pane's active tab (WorkspaceController::moveTab's
        # insertTab always activates it).
        wait_until(lambda: self.app.panes.b.navigation.address_bar.text.endswith("a1"))


class TestCrossPaneTabDragGuard(ExpLorerConfiguredFreshAppTestCase):
    """A pane must always retain at least one tab -- dragging a pane's only tab onto another pane
    must not move it (WorkspaceController::moveTab's own tabCount() <= 1 rejection).
    """

    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig, tmp_path: Path) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        a1 = tmp_path / "a1"
        b1 = tmp_path / "b1"
        a1.mkdir()
        b1.mkdir()
        config.workspace.panes.a.add_tab(path=str(a1))
        config.workspace.panes.b.add_tab(path=str(b1))

    def test_dragging_a_panes_only_tab_does_not_move_it(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a1"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1"])

        drag_tab_to_other_pane(
            self.app.panes.a.tabs, source_index=0, target_point=_content_area_point(self.app.panes.b)
        )

        # Give the (rejected) drag a moment to fully resolve before asserting nothing changed.
        with pytest.raises(TimeoutError):
            wait_until(lambda: self.app.panes.a.tabs.titles() != ["a1"], timeout=1.5)

        assert self.app.panes.a.tabs.titles() == ["a1"]
        assert self.app.panes.b.tabs.titles() == ["b1"]


class TestMoveToPaneContextMenu(ExpLorerConfiguredFreshAppTestCase):
    """The non-drag "Move to Pane" context-menu alternative for the same cross-pane move."""

    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig, tmp_path: Path) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        a1 = tmp_path / "a1"
        a2 = tmp_path / "a2"
        b1 = tmp_path / "b1"
        a1.mkdir()
        a2.mkdir()
        b1.mkdir()
        config.workspace.panes.a.add_tab(path=str(a1)).add_tab(path=str(a2))
        config.workspace.panes.b.add_tab(path=str(b1))

    def test_menu_lists_other_visible_panes_but_not_the_source_pane(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a1", "a2"])

        tab_item = self.app.panes.a.tabs.item(0)
        menu = open_context_menu(tab_item)
        try:
            submenu = menu.open_submenu("Move to Pane")
            try:
                assert "Pane B" in submenu.item_texts()
                assert "Pane A" not in submenu.item_texts()
            finally:
                submenu.close()
        finally:
            menu.close()

    def test_clicking_a_pane_entry_moves_the_tab_there(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a1", "a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1"])

        tab_item = self.app.panes.a.tabs.item(0)
        menu = open_context_menu(tab_item)
        submenu = menu.open_submenu("Move to Pane")
        submenu.click("Pane B")

        wait_until(lambda: self.app.panes.a.tabs.titles() == ["a2"])
        wait_until(lambda: self.app.panes.b.tabs.titles() == ["b1", "a1"])


class TestMoveToPaneContextMenuOmitted(ExpLorerFreshAppTestCase):
    """The menu is omitted entirely (no popup at all, not a disabled entry) when the move would
    be a no-op: the default Single layout's lone pane with its lone default tab.
    """

    def test_right_clicking_the_only_tab_in_single_layout_shows_no_menu(self) -> None:
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["This PC"])

        tab_item = self.app.panes.a.tabs.item(0)
        before = visible_top_level_window_handles()
        tab_item.right_click_input()

        with pytest.raises(TimeoutError):
            wait_for_new_top_level_window(before, timeout=1.5)
