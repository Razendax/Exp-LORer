"""Coverage for MainWindow::closeEvent persisting the live workspace state to config.json on exit
(Architecture.md §14.14) -- layout, tab navigation, sort order, and Details-view column widths.

Starts from an `ExpLorerConfig`-seeded config.json (rather than the `app` fixture) so each test
controls exactly what should/shouldn't have changed by the time the app closes and re-saves it.
"""

from __future__ import annotations

import json

from framework import ExpLorerApp, ExpLorerConfig


def _load_saved_config(data_dir):
    return json.loads((data_dir / "config.json").read_text(encoding="utf-8"))


def test_closing_saves_navigation_layout_and_sort_order(exe_path, tmp_path):
    dir_a = tmp_path / "a"
    dir_b = tmp_path / "b"
    dir_a.mkdir()
    dir_b.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(dir_a))
    config.workspace.save()

    ExpLorerApp.kill_stray_instances()
    app = ExpLorerApp.start(data_dir=data_dir, exe_path=exe_path)
    try:
        app.navigation.navigate_to(str(dir_b))

        tree = app.panes.a.file_tree_view.raw
        size_header = next(h for h in tree.children() if h.window_text() == "Size")
        size_header.click_input()  # switch sort criterion away from the Name default

        app.layout.two_vertical.click()
    finally:
        app.close()

    saved = _load_saved_config(data_dir)
    assert saved["workspace"]["layout"] == "TwoVertical"
    pane_a_tab = saved["workspace"]["panes"]["PaneA"]["tabs"][0]
    assert pane_a_tab["path"] == str(dir_b)
    assert pane_a_tab["sortCriterion"] == "Size"


def test_closing_saves_toggled_sort_direction(exe_path, tmp_path):
    target = tmp_path / "target"
    target.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target))  # default sortAscending=True
    config.workspace.save()

    ExpLorerApp.kill_stray_instances()
    app = ExpLorerApp.start(data_dir=data_dir, exe_path=exe_path)
    try:
        tree = app.panes.a.file_tree_view.raw
        name_header = next(h for h in tree.children() if h.window_text() == "Name")
        name_header.click_input()  # same criterion clicked again reverses direction
    finally:
        app.close()

    saved = _load_saved_config(data_dir)
    pane_a_tab = saved["workspace"]["panes"]["PaneA"]["tabs"][0]
    assert pane_a_tab["sortCriterion"] == "Name"
    assert pane_a_tab["sortAscending"] is False


def test_closing_saves_multiple_tabs_and_active_tab_index(exe_path, tmp_path):
    dir1 = tmp_path / "dir1"
    dir2 = tmp_path / "dir2"
    dir1.mkdir()
    dir2.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(dir1))
    config.workspace.save()

    ExpLorerApp.kill_stray_instances()
    app = ExpLorerApp.start(data_dir=data_dir, exe_path=exe_path)
    try:
        app.navigation.new_tab.click()
        app.navigation.navigate_to(str(dir2))
    finally:
        app.close()

    saved = _load_saved_config(data_dir)
    pane_a = saved["workspace"]["panes"]["PaneA"]
    assert [tab["path"] for tab in pane_a["tabs"]] == [str(dir1), str(dir2)]
    assert pane_a["activeTabIndex"] == 1


def test_closing_saves_details_column_widths_unchanged(exe_path, tmp_path):
    target = tmp_path / "target"
    target.mkdir()

    data_dir = tmp_path / "data"
    config = ExpLorerConfig(data_dir)
    config.workspace.panes.a.add_tab(path=str(target))
    config.details_column_widths = [321, 111, 91, 161, 41]
    config.workspace.save()

    ExpLorerApp.kill_stray_instances()
    app = ExpLorerApp.start(data_dir=data_dir, exe_path=exe_path)
    app.close()  # no column resize -- verifies the *existing* widths round-trip on close

    saved = _load_saved_config(data_dir)
    assert saved["detailsColumnWidths"][:4] == [321, 111, 91, 161]
