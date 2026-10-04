"""Coverage for Details-view multi-selection mechanics -- shift-click range select, ctrl-click
add/remove, Ctrl+A select-all, and the per-pane status bar's selection-count feedback
(Architecture.md §14.16, §14.31's `StatusBarWidget::setCounts`).

Uses `ExpLorerFreshAppTestCase`: each test asserts on an exact selection built up from scratch,
which a shared instance's leftover selection from a previous test method could perturb.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.details_view import select_all, select_row, selected_names
from framework.wait import wait_until


class TestMultiSelection(ExpLorerFreshAppTestCase):
    def test_shift_click_selects_a_contiguous_range(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        for name in ("a.txt", "b.txt", "c.txt", "d.txt"):
            (root / name).write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        tv = self.app.panes.a.file_tree_view

        select_row(tv, "a.txt")
        select_row(tv, "c.txt", extend=True)

        wait_until(lambda: set(selected_names(tv)) == {"a.txt", "b.txt", "c.txt"} or None)

    def test_ctrl_click_adds_and_removes_individual_items(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        for name in ("a.txt", "b.txt", "c.txt"):
            (root / name).write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        tv = self.app.panes.a.file_tree_view

        select_row(tv, "a.txt")
        select_row(tv, "b.txt", additive=True)
        select_row(tv, "c.txt", additive=True)
        wait_until(lambda: set(selected_names(tv)) == {"a.txt", "b.txt", "c.txt"} or None)

        # Ctrl-clicking an already-selected item removes it from the selection.
        select_row(tv, "b.txt", additive=True)
        wait_until(lambda: set(selected_names(tv)) == {"a.txt", "c.txt"} or None)

    def test_ctrl_a_selects_every_row_in_the_current_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        for name in ("a.txt", "b.txt", "c.txt"):
            (root / name).write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        tv = self.app.panes.a.file_tree_view
        select_row(tv, "a.txt")

        select_all(tv)
        wait_until(lambda: set(selected_names(tv)) == {"a.txt", "b.txt", "c.txt"} or None)

    def test_status_bar_reflects_selection_count(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        for name in ("a.txt", "b.txt", "c.txt"):
            (root / name).write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        tv = self.app.panes.a.file_tree_view
        status = self.app.panes.a.status_bar

        wait_until(lambda: status.counts_label.text == "3 items")

        select_row(tv, "a.txt")
        select_row(tv, "b.txt", additive=True)
        wait_until(lambda: status.counts_label.text == "3 items, 2 selected")

        select_all(tv)
        wait_until(lambda: status.counts_label.text == "3 items, 3 selected")


class TestPaneStatusBarQuickSelect(ExpLorerFreshAppTestCase):
    def test_quick_select_selects_every_entry_whose_name_contains_the_typed_text(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        for name in ("alpha.txt", "beta.txt", "alphabet.txt"):
            (root / name).write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        tv = self.app.panes.a.file_tree_view
        quick_select = self.app.panes.a.status_bar.quick_select_edit

        quick_select.click()
        quick_select.set_text("alpha")

        wait_until(lambda: set(selected_names(tv)) == {"alpha.txt", "alphabet.txt"} or None)
