"""Coverage for the Details view (QTreeView) correctly listing a folder's files/folders and their
Size/Type/Date-modified columns -- FileListModel.h, Architecture.md §2.3.1.
"""

from __future__ import annotations

import re

from framework.app import ExpLorerApp
from framework.details_view import find_row, read_details_rows
from framework.wait import wait_until

_DATE_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}$")  # FileListModel.cpp's formatDate()


def test_details_view_lists_files_and_folders_with_correct_type_and_size(app: ExpLorerApp, tmp_path):
    target = tmp_path / "browse"
    target.mkdir()
    (target / "notes.txt").write_text("hello")  # 5 bytes
    (target / "data.bin").write_bytes(b"\x00" * 1234)
    (target / "README").write_text("no extension")
    (target / "subfolder").mkdir()

    app.navigation.navigate_to(str(target))
    rows = wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) or None)

    assert len(rows) == 4

    folder_row = find_row(rows, "subfolder")
    assert folder_row.type == "File folder"
    assert folder_row.size == ""  # directories show no size (FileListModel::data(), SizeColumn)
    assert _DATE_PATTERN.match(folder_row.date_modified)

    txt_row = find_row(rows, "notes.txt")
    assert txt_row.type == "TXT File"
    assert txt_row.size == "5"

    bin_row = find_row(rows, "data.bin")
    assert bin_row.type == "BIN File"
    assert bin_row.size == "1234"

    no_ext_row = find_row(rows, "README")
    assert no_ext_row.type == "File"


def test_details_view_reflects_an_empty_folder(app: ExpLorerApp, tmp_path):
    target = tmp_path / "empty"
    target.mkdir()

    app.navigation.navigate_to(str(target))
    wait_until(lambda: read_details_rows(app.panes.a.file_tree_view) == [])
    assert read_details_rows(app.panes.a.file_tree_view) == []
