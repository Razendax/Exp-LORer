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

_COLUMNS_PER_ROW = 9  # FileListModel::ColumnCount

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
    "Fragments",
]


@dataclass(frozen=True)
class DetailsRow:
    """One row's cells, in FileListModel::Column order. `size` is the human-readable formatted
    text for a file (`FileSizeFormat::format`, e.g. "5 B"/"1.2 KB"/"48.8 MB"), and the same once
    cached for a directory with a known recursive size -- empty for a directory with no cached
    size yet. `percent` is the "NN%" text (empty when this entry's size, or the folder's total
    known size, is unknown).

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
    fragments: str


def read_details_rows(tree_view: Element) -> List[DetailsRow]:
    """Snapshot of every row currently shown in `tree_view` (a Details-view FileBrowserView's
    `file_tree_view`), in display order. Empty list for an empty folder/no results.

    Starts a new row whenever a cell's on-screen horizontal position matches the Name header's
    (Name is always the first, always-visible column -- see `DetailsRow`'s docstring on hidden
    columns), then pads that row with trailing "" up to `FileListModel::ColumnCount` (9) cells,
    rather than assuming every row always contributes exactly 9 cells to the flat stream.
    Confirmed against a real build that at some window sizes the tree is narrower than its own 9
    columns' combined width, so trailing columns (commonly Percent/Fragments) are scrolled out
    of the current viewport and contribute no cell at all -- with a fixed 9-wide stride, that
    silently drops every row as a short, incomplete trailing chunk instead of just leaving a
    couple of trailing fields blank. A column hidden *mid-row* (not merely scrolled out of view)
    still misaligns every field after it, same as before -- use `read_header_titles` instead once
    column visibility might have changed, per this module's existing guidance.
    """
    name_left = find_header(tree_view, "Name").rectangle().left
    rows: List[List[str]] = []
    for cell in tree_view.raw.children():
        if cell.element_info.control_type != "TreeItem":
            continue
        if abs(cell.rectangle().left - name_left) < 5:
            rows.append([])
        if rows:
            rows[-1].append(cell.window_text())
    return [
        DetailsRow(*(row + [""] * (_COLUMNS_PER_ROW - len(row)))[:_COLUMNS_PER_ROW])
        for row in rows
    ]


def find_row(rows: List[DetailsRow], name: str) -> DetailsRow:
    """Looks up a row by its Name cell -- a clearer failure than a bare index/KeyError when the
    row a test expects isn't present.
    """
    for row in rows:
        if row.name == name:
            return row
    raise LookupError(f"No details-view row named {name!r} among {[r.name for r in rows]}")


def select_row(
    tree_view: Element, name: str, additive: bool = False, extend: bool = False, timeout: float = 5
) -> None:
    """Clicks a Details-view row's Name cell to select it -- QItemSelectionModel::ClearAndSelect,
    which drives TabViewModel::selectedEntries and therefore what the tag panel resolves as the
    tagging target(s) (TagListViewModel::resolveTargets) / shows under "Tags on selection".

    `additive=True` holds Ctrl during the click (like a real ctrl-click), adding this row to
    whatever's already selected instead of replacing it -- for building a multi-selection.
    `extend=True` holds Shift instead (like a real shift-click), selecting the contiguous range
    from the previous anchor row to this one (Qt's own `ExtendedSelection` range-select, not
    anything this framework computes itself). Mutually exclusive with `additive`.

    Waits for the selection to actually change (via `selected_names`) before returning --
    otherwise a caller that immediately reads selection-dependent state (e.g. the Tag Panel's
    "Tags on selection", which resolves off `TabViewModel::selectedEntries`) can race a selection
    that was clicked but hasn't been delivered/processed yet, and transiently see the *previous*
    selection's state instead. Compares against the selection from just before the click rather
    than asserting `name` ends up selected -- an `additive` (Ctrl-click) click on an already-
    selected row *removes* it instead, so `name` itself may not be part of the new selection.
    """
    modifier = "control" if additive else ("shift" if extend else "")
    deadline = time.monotonic() + timeout
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            before = selected_names(tree_view)
            for child in tree_view.raw.children():
                if child.element_info.control_type == "TreeItem" and child.window_text() == name:
                    child.click_input(pressed=modifier)
                    wait_until(lambda: selected_names(tree_view) != before, timeout=timeout)
                    return
        except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
            last_error = exc
        time.sleep(0.15)
    raise LookupError(f"No details-view row named {name!r} to select ({last_error})")


def selected_names(tree_view: Element) -> List[str]:
    """Display names of every currently selected row, top to bottom. `FileBrowserView`'s
    `SelectRows` selection behavior (Architecture.md §14.16) means selecting any cell selects
    every cell in that row, so checking just each row's Name cell is enough.

    Identifies each row's Name cell by on-screen horizontal position (like
    `read_name_column`) rather than by a fixed stride through the flat cell list -- confirmed
    against a real build that this view's cell-per-row count isn't reliably 8
    (`FileListModel::ColumnCount`): at some window sizes trailing columns are scrolled out of the
    tree's current viewport width and contribute no cell at all, which would otherwise silently
    misalign a fixed-stride reader onto the wrong column.
    """
    name_rect = find_header(tree_view, "Name").rectangle()
    names = []
    for cell in tree_view.raw.children():
        if cell.element_info.control_type != "TreeItem" or abs(cell.rectangle().left - name_rect.left) >= 5:
            continue
        try:
            if cell.is_selected():
                names.append(cell.window_text())
        except Exception:  # noqa: BLE001 -- row without SelectionItem pattern
            continue
    return names


def row_item(tree_view: Element, name: str, timeout: float = 5) -> Any:
    """Raw pywinauto wrapper for the Details-view row named `name` (its Name-column `TreeItem`)
    -- for right-click/double-click/drag use, or any pywinauto API `select_row`/`DetailsRow`
    don't cover. Same retry loop as `select_row`, since the row set can be transiently
    unreachable mid-rebuild.
    """
    deadline = time.monotonic() + timeout
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            for child in tree_view.raw.children():
                if child.element_info.control_type == "TreeItem" and child.window_text() == name:
                    return child
        except Exception as exc:  # noqa: BLE001 -- transient UIA COM errors mid-rebuild
            last_error = exc
        time.sleep(0.15)
    raise LookupError(f"No details-view row named {name!r} ({last_error})")


def double_click_row(tree_view: Element, name: str, timeout: float = 5) -> None:
    """Double-clicks a Details-view row's Name cell -- `QAbstractItemView::activated`, the same
    signal Enter-on-a-selected-row fires (see `activate_selection_with_enter`), opening a file
    with its OS-registered default application or navigating into a folder
    (`FileBrowserView::itemActivated`).

    Uses a raw-Win32 double-click at the cell's screen coordinates (`focus.double_click_at`)
    rather than the wrapper's own `double_click_input()` or `pywinauto.mouse.double_click` --
    confirmed against a real build that neither of those reliably lands as an OS-level double
    click here (Qt never sees `QEvent::MouseButtonDblClick`, so `activated` never fires). Also
    re-forces the main window to the foreground first (`focus.bring_to_foreground`) -- confirmed
    by hand that without it, `activated` can silently never fire even with the right double-click
    technique.
    """
    from .focus import bring_to_foreground, double_click_at

    item = row_item(tree_view, name, timeout=timeout)
    bring_to_foreground(item)
    rect = item.rectangle()
    double_click_at((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)


def activate_selection_with_enter(tree_view: Element) -> None:
    """Sends Enter to `tree_view`'s current selection -- `QAbstractItemView::activated`, same
    effect as `double_click_row` (see its docstring). The view must already have keyboard focus
    and a selection (e.g. via `select_row`).
    """
    from pywinauto.keyboard import send_keys

    from .focus import bring_to_foreground

    bring_to_foreground(tree_view.raw)
    tree_view.raw.set_focus()
    send_keys("{ENTER}")


def select_all(tree_view: Element) -> None:
    """Sends Ctrl+A to `tree_view`, selecting every row in the folder it's currently browsing."""
    from pywinauto.keyboard import send_keys

    tree_view.raw.set_focus()
    send_keys("^a")


def copy_selection(tree_view: Element) -> None:
    """Sends Ctrl+C to `tree_view`, copying its current selection to the OS clipboard --
    `FileBrowserView::handleKeyPress`'s `QKeySequence::Copy` handling
    (`FileOperationsController::copyToClipboard`). Selection must already be made (e.g. via
    `select_row`), which also gives the view keyboard focus.
    """
    from pywinauto.keyboard import send_keys

    tree_view.raw.set_focus()
    send_keys("^c")


def cut_selection(tree_view: Element) -> None:
    """Like `copy_selection`, but `QKeySequence::Cut` (`FileOperationsController::cutToClipboard`)
    -- marks the clipboard as a move rather than a copy.
    """
    from pywinauto.keyboard import send_keys

    tree_view.raw.set_focus()
    send_keys("^x")


def paste_clipboard(tree_view: Element) -> None:
    """Sends Ctrl+V to `tree_view`, pasting whatever's on the OS clipboard into the folder it's
    currently browsing (`QKeySequence::Paste` -> `FileOperationsController::pasteInto`). A no-op
    (no crash) if the clipboard holds no file URLs.
    """
    from pywinauto.keyboard import send_keys

    tree_view.raw.set_focus()
    send_keys("^v")


def delete_selection(tree_view: Element, permanent: bool = False) -> None:
    """Sends Delete (or Shift+Delete for `permanent`) to `tree_view`, raising the same
    confirmation dialog the context menu's "Delete" entry does -- answer it with
    `confirm_delete_dialog`.
    """
    from pywinauto.keyboard import send_keys

    tree_view.raw.set_focus()
    send_keys("+{DELETE}" if permanent else "{DELETE}")


def confirm_delete_dialog(permanent: bool = False, accept: bool = True, timeout: float = 5) -> None:
    """Answers the `QMessageBox::question` delete-confirmation dialog
    (`FileItemContextMenu::confirmAndDelete`) raised by `delete_selection` or the context menu's
    "Delete" entry -- "Yes" if `accept`, else "No" (leaves every item untouched). Titled "Delete
    Permanently" for a permanent delete, else "Delete".
    """
    from .modal_dialog import connect_by_handle, find_control, wait_for_window_handle

    title = "Delete Permanently" if permanent else "Delete"
    dialog = connect_by_handle(wait_for_window_handle(title, timeout=timeout))
    find_control(dialog, "Button", "Yes" if accept else "No").click_input()


def begin_rename(tree_view: Element, name: str) -> None:
    """Selects `name` and presses F2, opening the Details-view's inline `FileNameEditDelegate`
    editor -- same entry point the context menu's "Rename" action and `rename` below use. Split
    out from `rename` so a test can inspect/cancel the open editor before committing.

    Unlike `test_left_panel_tree.py`'s `FolderTree`, this editor can't be located as a pywinauto
    element afterwards: confirmed against a real build (a file-based debug trace inside
    `FileBrowserView::handleKeyPress`) that `QAbstractItemView::edit()` *does* create and show a
    real, focused `QLineEdit` child of the viewport here, but Qt's `uia` accessibility bridge
    never surfaces it anywhere in the window's UI Automation tree -- apparently specific to this
    multi-column `QTreeView` (the single-column folder tree's equivalent editor IS exposed).
    `commit_rename`/`cancel_rename` below therefore target it blindly via keystrokes sent to
    whatever currently has OS keyboard focus, like `FolderTree.rename` does, rather than
    resolving an `Element` for it.
    """
    from pywinauto.keyboard import send_keys

    select_row(tree_view, name)
    tree_view.raw.set_focus()
    send_keys("{F2}")
    time.sleep(0.3)  # lets the editor actually open before keys start arriving


def commit_rename(new_name: str) -> None:
    """Replaces the open rename editor's text with `new_name` and commits with Enter. Keys go to
    whatever has focus (the editor opened by `begin_rename`) -- see its docstring for why this
    can't target the editor by reference. `pywinauto.keyboard.send_keys` falls back to Unicode
    key events for characters it can't map through the active keyboard layout, so non-Latin names
    within the Basic Multilingual Plane (Cyrillic, CJK, ...) round-trip correctly; characters
    outside it (e.g. emoji, which need a UTF-16 surrogate pair) don't -- avoid those in a typed
    `new_name`.
    """
    from pywinauto.keyboard import send_keys

    send_keys("^a" + new_name + "{ENTER}", with_spaces=True)


def cancel_rename() -> None:
    """Cancels the open rename editor with Escape, discarding any typed text."""
    from pywinauto.keyboard import send_keys

    send_keys("{ESC}")


def rename(tree_view: Element, name: str, new_name: str) -> None:
    """Convenience: `begin_rename` + `commit_rename` in one call, for the common case of a rename
    expected to succeed outright (see `test_left_panel_tree.py`'s `FolderTree.rename` for the
    folder-tree equivalent).
    """
    begin_rename(tree_view, name)
    commit_rename(new_name)


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


def read_name_column(tree_view: Element) -> List[str]:
    """Display names of every currently shown row, top to bottom -- reads the Name cell
    specifically by on-screen horizontal position rather than by a fixed stride through the flat
    cell stream (what `read_details_rows` does). Robust even when trailing columns are scrolled
    out of the tree's current viewport width and so contribute no cell at all (confirmed against
    a real build: a view narrower than its own 8 columns' combined width -- e.g. the Advanced
    Search results pane at this app's default window size -- can end up with fewer cells per row
    than `FileListModel::ColumnCount`, which would silently misalign `read_details_rows`' fixed
    8-wide grouping). Safe to use any time only the Name column is actually needed.
    """
    name_rect = find_header(tree_view, "Name").rectangle()
    return [
        cell.window_text()
        for cell in tree_view.raw.children()
        if cell.element_info.control_type == "TreeItem" and abs(cell.rectangle().left - name_rect.left) < 5
    ]


def read_grid_item_names(list_view: Element) -> List[str]:
    """Display names of every currently shown row in an icon-grid `QListView` (`FILE_LIST_VIEW` /
    `app.panes.a.file_list_view`) -- e.g. Media Viewer mode's locked icon-grid rendering
    (`FileBrowserView::DisplayMode::MediaViewerGrid`, Architecture.md §14.61). Unlike the
    `QTreeView`-backed Details view `read_name_column`/`read_details_rows` read from, Qt's `uia`
    bridge exposes each `QListView` row as one flat `ListItem` child (one cell, not one per
    column) -- the same shape `PANE_ADDRESS_BAR_POPUP`'s `QListWidget` items use.
    """
    return [
        item.window_text() for item in list_view.raw.children() if item.element_info.control_type == "ListItem"
    ]


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
