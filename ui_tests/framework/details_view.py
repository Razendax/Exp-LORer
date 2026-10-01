"""Reads Details-view (QTreeView) row/header data out of a FileBrowserView.

Qt's `uia` accessibility bridge doesn't expose QTreeView's per-row/per-cell grid structure --
every column of every row surfaces as its own `TreeItem`, listed flat (row-major, column order) as
direct children of the tree, alongside one `Header` child per column (confirmed against this
repo's Qt/pywinauto versions; there's no public Qt/UIA doc guaranteeing it). This module regroups
that flat stream back into rows of `FileListModel::ColumnCount` (8) cells each --
Name/Size/Type/DateModified/GitStatus/Tags/DateCreated/Percent, in that order
(src/adapters/viewmodels/FileListModel.h). A *hidden* column's `Header` cell stays present in that
flat stream too, collapsed to a zero-size rectangle rather than removed -- see `_header_items`'s
docstring.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, List, Optional

from .element import Element
from .wait import wait_until

_COLUMNS_PER_ROW = 8  # FileListModel::ColumnCount

# Every FileListModel::Column except Name, in FileListModel::Column enum order -- mirrors
# SettingsDialog.cpp's kAllVisibilityColumns and FileBrowserView::showColumnMenu's own loop order
# (both start at NameColumn + 1). Name is excluded everywhere a column can be hidden/reordered in
# the *visibility* sense (it's always shown), but it CAN still be dragged to a different position
# (Architecture.md's column-order persistence treats every column, Name included, uniformly).
TOGGLEABLE_COLUMN_TITLES: List[str] = [
    "Size",
    "Type",
    "Date modified",
    "Git",
    "Tags",
    "Date created",
    "Percent",
]


@dataclass(frozen=True)
class DetailsRow:
    """One row's cells, in FileListModel::Column order. `size` is the raw displayed text -- a byte
    count for a file, empty for a directory with no cached recursive size (FileListModel::data()'s
    SizeColumn/DisplayRole handling never formats it as "1 KB" etc.). `percent` is the "NN%" text
    (empty when this entry's size, or the folder's total known size, is unknown).

    Reading rows this way assumes every column is currently visible (the default) -- if a column
    has been hidden (see `toggle_column_visibility`), its cell simply isn't part of the flat stream
    `read_details_rows` groups, which would misalign every field after it. Use `read_header_titles`
    instead of `read_details_rows` once column visibility might have changed.
    """

    name: str
    size: str
    type: str
    date_modified: str
    git_status: str
    tags: str
    date_created: str
    percent: str


def read_details_rows(tree_view: Element) -> List[DetailsRow]:
    """Snapshot of every row currently shown in `tree_view` (a Details-view FileBrowserView's
    `file_tree_view`/`quick_search_results_view`), in display order. Empty list for an empty
    folder/no results. Assumes every column is visible -- see `DetailsRow`'s docstring.
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


def _header_items(tree_view: Element) -> List[Any]:
    """Every currently-*visible* column header cell in `tree_view`'s header row, left to right.

    A hidden column's header cell (`FileBrowserView::setColumnVisible(column, false)`) is NOT
    dropped from `.children()` -- confirmed against a real build (right-click menu -> uncheck a
    column -> re-read `.children()`): it stays present but collapses to a zero-size `(0, 0, 0, 0)`
    rectangle, which would otherwise sort first/last unpredictably (its `.left` of `0` isn't
    meaningfully comparable to the other cells' real screen coordinates) and falsely appear to
    still be "there" to any caller checking cell titles/count. Filtering by rectangle width is what
    actually reflects visibility; sorting by `.left` alone (what a hidden cell's raw presence in
    `.children()` might suggest) is not enough.
    """
    items = [child for child in tree_view.raw.children() if child.element_info.control_type == "Header"]
    visible = [item for item in items if item.rectangle().right > item.rectangle().left]
    return sorted(visible, key=lambda item: item.rectangle().left)


def read_header_titles(tree_view: Element) -> List[str]:
    """The currently visible Details-view column titles, left to right -- the one reliable way to
    check column visibility/order once columns might have been hidden or reordered (see
    `DetailsRow`'s docstring for why `read_details_rows` can't be used for that).
    """
    return [item.window_text() for item in _header_items(tree_view)]


def find_header(tree_view: Element, title: str) -> Any:
    """Locates one header cell by its exact title (e.g. "Size", "Date modified") -- the raw
    pywinauto wrapper, ready for `.click_input()` (sort), `.right_click_input()` (column-visibility
    menu), or `.drag_mouse_input(dst=...)` (reordering).
    """
    for item in _header_items(tree_view):
        if item.window_text() == title:
            return item
    raise LookupError(f"No details-view header named {title!r} among {read_header_titles(tree_view)}")


def drag_header(tree_view: Element, from_title: str, to_title: str, steps: int = 12, step_delay: float = 0.05) -> None:
    """Drags the `from_title` column header onto `to_title`'s current position, reordering
    Details-view columns (FileBrowserView::setColumnOrder is driven by QHeaderView's own
    setSectionsMovable(true) drag-reorder, not called directly). Manually steps the mouse across in
    small increments and lands 80% of the way into the destination header's width (past its visual
    midpoint) instead of pywinauto's canned `drag_mouse_input` (a press, a few 1px wiggles near the
    start, then one single big jump straight to the destination) -- verified by hand against a real
    build that QHeaderView's live drag-reorder tracking needs a steady stream of intermediate
    mouseMoveEvents, and that the cursor has to clearly cross past a section's midpoint before a
    swap actually commits; the canned single-jump approach did neither and never reordered anything.

    Re-locates `to_title`'s header on every step rather than targeting a single rectangle captured
    before the drag started: QHeaderView's live reorder preview can shift intervening sections (and
    therefore `to_title`'s position) while the drag is in progress, which would otherwise leave the
    mouse releasing over a stale, no-longer-correct destination.
    """
    source = find_header(tree_view, from_title)
    src_rect = source.rectangle()
    y = (src_rect.top + src_rect.bottom) // 2
    x_start = (src_rect.left + src_rect.right) // 2

    def target_x() -> int:
        dest_rect = find_header(tree_view, to_title).rectangle()
        return dest_rect.left + int((dest_rect.right - dest_rect.left) * 0.8)

    source.press_mouse_input(coords=(x_start, y))
    time.sleep(0.2)
    for i in range(1, steps + 1):
        x = x_start + (target_x() - x_start) * i // steps
        source.move_mouse_input(coords=(x, y))
        time.sleep(step_delay)
    time.sleep(0.2)
    source.release_mouse_input(coords=(target_x(), y))


def toggle_column_visibility(tree_view: Element, column_title: str) -> None:
    """Right-clicks `tree_view`'s header (opening FileBrowserView::showColumnMenu's per-tab
    column-visibility QMenu) and toggles the checkable entry for `column_title` on/off.

    Right-clicks the "Name" header cell specifically -- always visible/present regardless of which
    column is being toggled (including re-showing an already-hidden one) -- since any point on the
    header opens the same menu (`customContextMenuRequested`'s position only affects where the menu
    is drawn, not its contents).

    Navigates the open popup by KEYBOARD (Down * position + Enter) rather than clicking an entry by
    screen position/title -- both were tried against a real build and neither worked reliably here:
    a mouse click at the menu item's own reported center never toggled anything (this machine's
    popup rendered on a secondary monitor at negative virtual-screen coordinates, which may be why),
    and locating the popup to click into wasn't reliable either (unlike the app's other QAction-only
    popups, e.g. favorites_menu.py/tag_manager_dialog.py, this one made the app's own top-level
    window title briefly ambiguous to search through). Keyboard Down/Enter needs no coordinates and
    was confirmed (via captured screenshots) to select and activate the intended item. The menu's
    entries are inserted in `TOGGLEABLE_COLUMN_TITLES` order (`FileBrowserView::showColumnMenu`'s
    own loop order, `NameColumn + 1 .. ColumnCount`), with no initial selection, so `n` Down presses
    highlights the item at index `n - 1`.
    """
    from pywinauto.keyboard import send_keys

    index = TOGGLEABLE_COLUMN_TITLES.index(column_title)
    find_header(tree_view, "Name").right_click_input()
    time.sleep(0.3)  # lets the popup actually open before keys start arriving
    send_keys("{DOWN}" * (index + 1) + "{ENTER}")
