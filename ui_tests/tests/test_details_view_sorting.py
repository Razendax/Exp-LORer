"""Coverage for Details-view (QTreeView) column-header sorting -- FileListModel::sort()/
setSortCriterion, Architecture.md §2.3.1 -- and for the active sort criterion/direction persisting
on a tab when it navigates to a different folder (FileListModel's sort state lives on the model,
untouched by TabViewModel::navigateTo/loadAndApply).

Uses the `app` fixture (fresh Exp-LORer instance per test) rather than `ExpLorerTestCase` -- each
test relies on the tab's sort state starting at the known default (Name, ascending), which a
shared instance carrying a previous test method's clicked-header state would make ambiguous.

File sizes are deliberately picked so that name-ascending order (the tab's default) is NOT already
monotonic in size -- e.g. "a_large"/"b_small"/"c_medium" sizes as [5000, 10, 500] -- so a passing
size-sort assertion actually proves a re-sort happened rather than coincidentally matching the
untouched default order.
"""

from __future__ import annotations

from pathlib import Path
from typing import List

from framework.app import ExpLorerApp
from framework.details_view import read_details_rows
from framework.element import Element
from framework.wait import wait_until


def _click_header(tree_view: Element, title: str) -> None:
    header = next(h for h in tree_view.raw.children() if h.window_text() == title)
    header.click_input()


def _wait_for_row_count(tree_view: Element, count: int) -> List:
    def predicate():
        rows = read_details_rows(tree_view)
        return rows if len(rows) == count else None

    return wait_until(predicate)


def _make_three_files_with_non_monotonic_name_order(target: Path) -> None:
    target.mkdir()
    (target / "a_large.txt").write_bytes(b"x" * 5000)
    (target / "b_small.txt").write_bytes(b"x" * 10)
    (target / "c_medium.txt").write_bytes(b"x" * 500)


def test_clicking_size_header_sorts_rows_by_size(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "sort_by_size"
    _make_three_files_with_non_monotonic_name_order(target)

    app.navigation.navigate_to(str(target))
    _wait_for_row_count(app.panes.a.file_tree_view, 3)

    _click_header(app.panes.a.file_tree_view, "Size")

    rows = _wait_for_row_count(app.panes.a.file_tree_view, 3)
    sizes = [int(row.size) for row in rows]
    assert sizes == sorted(sizes) or sizes == sorted(sizes, reverse=True)


def test_clicking_same_header_twice_reverses_sort_direction(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "toggle_direction"
    _make_three_files_with_non_monotonic_name_order(target)

    app.navigation.navigate_to(str(target))
    _wait_for_row_count(app.panes.a.file_tree_view, 3)

    _click_header(app.panes.a.file_tree_view, "Size")
    first_rows = _wait_for_row_count(app.panes.a.file_tree_view, 3)
    first_order = [row.name for row in first_rows]

    _click_header(app.panes.a.file_tree_view, "Size")
    second_rows = _wait_for_row_count(app.panes.a.file_tree_view, 3)
    second_order = [row.name for row in second_rows]

    assert second_order == list(reversed(first_order))


def test_sort_order_persists_when_navigating_to_another_folder(app: ExpLorerApp, tmp_path: Path) -> None:
    first_dir = tmp_path / "first"
    second_dir = tmp_path / "second"
    _make_three_files_with_non_monotonic_name_order(first_dir)
    second_dir.mkdir()
    (second_dir / "d_large.txt").write_bytes(b"x" * 9000)
    (second_dir / "e_small.txt").write_bytes(b"x" * 20)
    (second_dir / "f_medium.txt").write_bytes(b"x" * 700)

    app.navigation.navigate_to(str(first_dir))
    _wait_for_row_count(app.panes.a.file_tree_view, 3)
    _click_header(app.panes.a.file_tree_view, "Size")

    first_rows = _wait_for_row_count(app.panes.a.file_tree_view, 3)
    first_sizes = [int(row.size) for row in first_rows]
    ascending = first_sizes == sorted(first_sizes)
    assert ascending or first_sizes == sorted(first_sizes, reverse=True)

    app.navigation.navigate_to(str(second_dir))
    second_rows = _wait_for_row_count(app.panes.a.file_tree_view, 3)
    second_sizes = [int(row.size) for row in second_rows]

    # No header click here -- the *same* sort criterion/direction set on `first_dir` must already
    # apply to `second_dir`'s listing, without the test re-clicking "Size".
    assert (second_sizes == sorted(second_sizes)) == ascending
    assert second_sizes == sorted(second_sizes) or second_sizes == sorted(second_sizes, reverse=True)
