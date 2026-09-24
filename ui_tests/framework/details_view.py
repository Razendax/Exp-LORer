"""Reads Details-view (QTreeView) row data out of a FileBrowserView.

Qt's `uia` accessibility bridge doesn't expose QTreeView's per-row/per-cell grid structure --
every column of every row surfaces as its own `TreeItem`, listed flat (row-major, column order) as
direct children of the tree, alongside one `Header` child per column (confirmed against this
repo's Qt/pywinauto versions; there's no public Qt/UIA doc guaranteeing it). This module regroups
that flat stream back into rows of `FileListModel::ColumnCount` (5) cells each -- Name/Size/Type/
DateModified/GitStatus, in that order (src/adapters/viewmodels/FileListModel.h).
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import List, Optional

from .element import Element

_COLUMNS_PER_ROW = 5  # FileListModel::ColumnCount


@dataclass(frozen=True)
class DetailsRow:
    """One row's cells, in FileListModel::Column order. `size` is the raw displayed text -- a byte
    count for a file, empty for a directory (FileListModel::data()'s SizeColumn/DisplayRole
    handling never formats it as "1 KB" etc.).
    """

    name: str
    size: str
    type: str
    date_modified: str
    git_status: str


def read_details_rows(tree_view: Element) -> List[DetailsRow]:
    """Snapshot of every row currently shown in `tree_view` (a Details-view FileBrowserView's
    `file_tree_view`/`quick_search_results_view`), in display order. Empty list for an empty
    folder/no results.
    """
    cells = [
        child.window_text()
        for child in tree_view.raw.children()
        if child.element_info.control_type != "Header"
    ]
    return [
        DetailsRow(*cells[i : i + _COLUMNS_PER_ROW])
        for i in range(0, len(cells), _COLUMNS_PER_ROW)
        if i + _COLUMNS_PER_ROW <= len(cells)
    ]


def find_row(rows: List[DetailsRow], name: str) -> DetailsRow:
    """Looks up a row by its Name cell -- a clearer failure than a bare index/KeyError when the
    row a test expects isn't present.
    """
    for row in rows:
        if row.name == name:
            return row
    raise LookupError(f"No details-view row named {name!r} among {[r.name for r in rows]}")


def select_row(tree_view: Element, name: str, additive: bool = False, timeout: float = 5) -> None:
    """Clicks a Details-view row's Name cell to select it -- QItemSelectionModel::ClearAndSelect,
    which drives TabViewModel::selectedEntries and therefore what the tag panel resolves as the
    tagging target (TagListViewModel::resolveTarget) / shows under "Tags on selection".

    `additive=True` holds Ctrl during the click (like a real ctrl-click), adding this row to
    whatever's already selected instead of replacing it -- for building a multi-selection.
    """
    deadline = time.monotonic() + timeout
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            for child in tree_view.raw.children():
                if child.element_info.control_type == "TreeItem" and child.window_text() == name:
                    child.click_input(pressed="control" if additive else "")
                    return
        except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
            last_error = exc
        time.sleep(0.15)
    raise LookupError(f"No details-view row named {name!r} to select ({last_error})")
