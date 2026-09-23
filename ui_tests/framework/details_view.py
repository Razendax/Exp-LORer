"""Reads Details-view (QTreeView) row data out of a FileBrowserView.

Qt's `uia` accessibility bridge doesn't expose QTreeView's per-row/per-cell grid structure --
every column of every row surfaces as its own `TreeItem`, listed flat (row-major, column order) as
direct children of the tree, alongside one `Header` child per column (confirmed against this
repo's Qt/pywinauto versions; there's no public Qt/UIA doc guaranteeing it). This module regroups
that flat stream back into rows of `FileListModel::ColumnCount` (5) cells each -- Name/Size/Type/
DateModified/GitStatus, in that order (src/adapters/viewmodels/FileListModel.h).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List

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
