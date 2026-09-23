"""Coverage for booting straight into a saved config.json (Architecture.md §14.14) --
multiple tabs per pane with a non-zero active tab index, per-tab view mode, real filesystem paths
(including non-ASCII, a hard requirement per Specification.md line 34), and Details-view column
widths.

Uses `ExpLorerConfig`/the `start_app` fixture rather than the plain `app` fixture (which starts
with no config.json) -- config.py's own docstring: the config must be written *before* the app
process starts. `start_app`/`tmp_path` come from conftest.py; `start_app` also owns killing stray
instances and closing the app afterwards.
"""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from framework import ExpLorerApp, ExpLorerConfig, SplitLayout, read_details_rows


def test_multiple_tabs_restore_with_correct_active_tab(
    start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
) -> None:
    dir1 = tmp_path / "dir1"
    dir2 = tmp_path / "dir2"
    dir3 = tmp_path / "dir3"
    for directory in (dir1, dir2, dir3):
        directory.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(dir1))
    config.workspace.panes.a.add_tab(path=str(dir2))
    config.workspace.panes.a.add_tab(path=str(dir3))
    config.workspace.panes.a.active_tab_index = 2
    config.workspace.save()

    app = start_app(data_dir)
    # The 3rd tab (index 2), not the 1st, must be the one actually selected/displayed.
    assert app.navigation.address_bar.text == str(dir3)


def test_two_pane_layout_restores_distinct_tab_per_pane(
    start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
) -> None:
    dir_a = tmp_path / "a"
    dir_b = tmp_path / "b"
    dir_a.mkdir()
    dir_b.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.layout = SplitLayout.TWO_VERTICAL
    config.workspace.panes.a.add_tab(path=str(dir_a))
    config.workspace.panes.b.add_tab(path=str(dir_b))
    config.workspace.save()

    app = start_app(data_dir)
    assert app.panes.a.file_browser.exists()
    assert app.panes.b.file_browser.exists()
    assert app.panes.a.navigation.address_bar.text == str(dir_a)
    assert app.panes.b.navigation.address_bar.text == str(dir_b)


def test_non_details_view_mode_restores_list_widget_not_tree(
    start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
) -> None:
    target = tmp_path / "target"
    target.mkdir()
    (target / "a.txt").write_text("x")

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target), view_mode="LargeIcons")
    config.workspace.save()

    app = start_app(data_dir)
    assert app.panes.a.file_list_view.exists()
    assert not app.panes.a.file_tree_view.exists()


def test_details_view_mode_restores_tree_widget(
    start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
) -> None:
    target = tmp_path / "target"
    target.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target), view_mode="Details")
    config.workspace.save()

    app = start_app(data_dir)
    assert app.panes.a.file_tree_view.exists()


def test_non_ascii_path_restores_without_crashing(
    start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
) -> None:
    # PathUtf8/non-ASCII handling is a hard requirement (Specification.md line 34,
    # Architecture.md §6) -- narrowing a non-ASCII path via path::string() throws and crashes the
    # whole app, so this is as much a "didn't crash" check as a "shows the right thing" one.
    target = tmp_path / "Папка_日本語_😀"
    target.mkdir()
    (target / "файл.txt").write_text("hi", encoding="utf-8")

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target))
    config.workspace.save()

    app = start_app(data_dir)
    assert app.exists()
    assert app.navigation.address_bar.text == str(target)
    rows = read_details_rows(app.panes.a.file_tree_view)
    assert [row.name for row in rows] == ["файл.txt"]


def test_details_column_widths_restore(start_app: Callable[[Path], ExpLorerApp], tmp_path: Path) -> None:
    target = tmp_path / "target"
    target.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target))
    config.details_column_widths = [300, 120, 90, 160, 40]
    config.workspace.save()

    app = start_app(data_dir)
    tree = app.panes.a.file_tree_view.raw
    widths = {
        header.window_text(): header.rectangle().width()
        for header in tree.children()
        if header.element_info.control_type == "Header"
    }
    assert widths["Name"] == 300
    assert widths["Size"] == 120
    assert widths["Type"] == 90
    assert widths["Date modified"] == 160
    # Git (the last column) stretches to fill the header's remaining width instead of honoring
    # its exact saved width -- QHeaderView's stretch-last-section behavior, not something
    # detailsColumnWidths controls -- so it's deliberately not asserted here.
