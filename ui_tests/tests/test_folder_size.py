"""Coverage for the "Folder Size" bottom-panel tab (Disk Usage / Folder Size feature,
src/adapters/viewmodels/FolderSizeViewModel.*, src/ui/widgets/FolderSizeWidget.*) -- "Scan Folder"
only, plus Details-view sorting by the Size/Percent columns it also populates.

Deliberately NOT covered here, per this feature's own scope cut and this suite's own instructions:
"Scan Drive" (recursively scans the whole drive -- far more expensive and not exercised by this
suite) and the pie chart (a hand-painted QPainter widget with no per-segment automation surface --
see framework/bottom_panel.py).
"""

from __future__ import annotations

from pathlib import Path
from typing import List

from framework import ExpLorerFreshAppTestCase, ExpLorerTestCase
from framework.details_view import DetailsRow, find_header, find_row, read_details_rows
from framework.wait import wait_until


def _make_folder_with_one_subfolder(target: Path) -> Path:
    target.mkdir()
    child = target / "child"
    child.mkdir()
    return child


def _wait_for_row_count(tree_view, count: int) -> List[DetailsRow]:
    def predicate():
        rows = read_details_rows(tree_view)
        return rows if len(rows) == count else None

    return wait_until(predicate)


def _wait_for_populated_size(tree_view, name: str, timeout: float = 20) -> DetailsRow:
    """Polls until `name`'s row shows a non-blank Size cell -- DiskUsageWorker's scan runs on a
    background thread, so the Details-view Size column only picks up the freshly-scanned/persisted
    result once the tab re-navigates (TabViewModel::requestFolderSizes, dispatched from the same
    directoryContentsChanged funnel a manual Refresh triggers -- a completed scan does not, by
    itself, push new data into FileListModel).
    """

    def predicate():
        rows = read_details_rows(tree_view)
        if not rows:
            return None
        row = find_row(rows, name)
        return row if row.size != "" else None

    return wait_until(predicate, timeout=timeout)


class TestFolderSizeAtThisPc(ExpLorerFreshAppTestCase):
    def test_scan_folder_button_disabled_while_browsing_this_pc(self) -> None:
        # A fresh instance's one default tab starts at the virtual "This PC" root
        # (VirtualPaths::ThisPC) -- FolderSizeWidget::updateButtonsEnabled() disables both scan
        # buttons there, since there's no real filesystem root to walk.
        assert self.app.panes.a.tabs.titles() == ["This PC"]
        assert not self.app.panes.a.bottom_panel.folder_size.scan_folder_button.is_enabled()


class TestFolderSizeScanFolder(ExpLorerTestCase):
    """Shared instance across the class -- each test method navigates to its own fresh `tmp_path`
    subfolder before asserting, so a previous method's scan/bottom-panel state never affects the
    next one's assertions (BottomPanel.folder_size is idempotent to re-select, and every read below
    is scoped to that test's own freshly-created folder).
    """

    def test_subfolder_size_is_blank_until_scanned(self, tmp_path: Path) -> None:
        target = tmp_path / "unscanned"
        child = _make_folder_with_one_subfolder(target)
        (child / "data.bin").write_bytes(b"x" * 2000)

        self.app.navigation.navigate_to(str(target))
        rows = _wait_for_row_count(self.app.panes.a.file_tree_view, 1)

        assert find_row(rows, "child").size == ""

    def test_scan_folder_populates_subfolder_size_after_refresh(self, tmp_path: Path) -> None:
        target = tmp_path / "scanned"
        child = _make_folder_with_one_subfolder(target)
        (child / "data.bin").write_bytes(b"x" * 300)
        (child / "more.bin").write_bytes(b"x" * 200)

        self.app.navigation.navigate_to(str(target))
        _wait_for_row_count(self.app.panes.a.file_tree_view, 1)

        self.app.panes.a.bottom_panel.folder_size.scan_folder()
        self.app.navigation.refresh.click()

        # A directory's Size cell is human-readable-formatted (FileSizeFormat::format, "12.3 MB"
        # style) unlike a file's raw byte count -- kept under 1 KB here so it stays "500 B", no
        # rounding to account for.
        row = _wait_for_populated_size(self.app.panes.a.file_tree_view, "child")
        assert row.size == "500 B"

    def test_scan_folder_button_re_enables_after_scan_completes(self, tmp_path: Path) -> None:
        target = tmp_path / "reenable"
        child = _make_folder_with_one_subfolder(target)
        (child / "data.bin").write_bytes(b"x" * 100)

        self.app.navigation.navigate_to(str(target))
        _wait_for_row_count(self.app.panes.a.file_tree_view, 1)

        folder_size = self.app.panes.a.bottom_panel.folder_size
        folder_size.scan_folder()  # waits for "Scan Folder" text to return, see FolderSizePanel

        assert folder_size.scan_folder_button.is_enabled()
        assert folder_size.scan_folder_button.text == "Scan Folder"


class TestPercentColumn(ExpLorerTestCase):
    """Shared instance -- see TestFolderSizeScanFolder's own docstring for why that's safe here."""

    def test_percent_column_shows_share_of_current_folder_total(self, tmp_path: Path) -> None:
        target = tmp_path / "percent_values"
        target.mkdir()
        (target / "ten.txt").write_bytes(b"x" * 100)
        (target / "thirty.txt").write_bytes(b"x" * 300)
        (target / "sixty.txt").write_bytes(b"x" * 600)

        self.app.navigation.navigate_to(str(target))
        rows = _wait_for_row_count(self.app.panes.a.file_tree_view, 3)

        assert find_row(rows, "ten.txt").percent == "10%"
        assert find_row(rows, "thirty.txt").percent == "30%"
        assert find_row(rows, "sixty.txt").percent == "60%"

    def test_clicking_percent_header_sorts_rows_by_percent(self, tmp_path: Path) -> None:
        # Same non-monotonic-by-name sizes test_details_view_sorting.py's Size-column test uses,
        # so a passing assertion actually proves a re-sort happened.
        target = tmp_path / "percent_sort"
        target.mkdir()
        (target / "a_large.txt").write_bytes(b"x" * 5000)
        (target / "b_small.txt").write_bytes(b"x" * 10)
        (target / "c_medium.txt").write_bytes(b"x" * 500)

        self.app.navigation.navigate_to(str(target))
        tree = self.app.panes.a.file_tree_view
        _wait_for_row_count(tree, 3)

        find_header(tree, "Percent").click_input()

        rows = _wait_for_row_count(tree, 3)
        percents = [int(row.percent.rstrip("%")) for row in rows]
        assert percents == sorted(percents) or percents == sorted(percents, reverse=True)
