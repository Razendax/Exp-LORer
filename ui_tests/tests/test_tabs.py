"""Coverage for switching between tabs within a pane (WorkspacePaneWidget's QTabWidget) and
navigating each tab independently -- tab titles reflecting the current folder, and each tab
keeping its own navigation state (current path, back/forward history) when the pane's active tab
switches away and back.

Uses the `app` fixture (fresh Exp-LORer instance per test) rather than `ExpLorerTestCase` -- a
shared instance would carry leftover tabs from a previous test method into the next one, making
the exact tab counts/indices these tests assert on unreliable.
"""

from __future__ import annotations

from pathlib import Path

from framework.app import ExpLorerApp
from framework.wait import wait_until


def test_new_tab_opens_at_this_pc(app: ExpLorerApp) -> None:
    assert app.panes.a.tabs.titles() == ["This PC"]

    app.navigation.new_tab.click()

    assert app.panes.a.tabs.titles() == ["This PC", "This PC"]
    assert app.navigation.address_bar.text == "This PC"


def test_tab_title_reflects_the_folder_navigated_to(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "my_folder"
    target.mkdir()

    app.navigation.navigate_to(str(target))

    wait_until(lambda: app.panes.a.tabs.titles() == ["my_folder"])


def test_switching_tabs_restores_each_tabs_own_location(app: ExpLorerApp, tmp_path: Path) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    first.mkdir()
    second.mkdir()

    app.navigation.navigate_to(str(first))
    app.navigation.new_tab.click()
    app.navigation.navigate_to(str(second))
    wait_until(lambda: app.panes.a.tabs.titles() == ["first", "second"])

    app.panes.a.tabs.select(0)
    wait_until(lambda: app.navigation.address_bar.text == str(first))

    app.panes.a.tabs.select(1)
    wait_until(lambda: app.navigation.address_bar.text == str(second))


def test_navigating_one_tab_does_not_affect_another(app: ExpLorerApp, tmp_path: Path) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    third = tmp_path / "third"
    for directory in (first, second, third):
        directory.mkdir()

    app.navigation.navigate_to(str(first))
    app.navigation.new_tab.click()
    app.navigation.navigate_to(str(second))
    app.navigation.navigate_to(str(third))  # further navigation within tab 2 only
    wait_until(lambda: app.panes.a.tabs.titles() == ["first", "third"])

    app.panes.a.tabs.select(0)
    wait_until(lambda: app.navigation.address_bar.text == str(first))


def test_each_tab_keeps_its_own_back_forward_history(app: ExpLorerApp, tmp_path: Path) -> None:
    a1 = tmp_path / "a1"
    a2 = tmp_path / "a2"
    b1 = tmp_path / "b1"
    for directory in (a1, a2, b1):
        directory.mkdir()

    app.navigation.navigate_to(str(a1))
    app.navigation.navigate_to(str(a2))
    assert app.navigation.back.is_enabled()  # tab 1 now has back history

    app.navigation.new_tab.click()
    assert not app.navigation.back.is_enabled()  # tab 2 is fresh, nothing to go back to yet
    app.navigation.navigate_to(str(b1))
    assert app.navigation.back.is_enabled()  # tab 2: This PC -> b1

    app.panes.a.tabs.select(0)
    wait_until(lambda: app.navigation.address_bar.text == str(a2))
    assert app.navigation.back.is_enabled()  # tab 1's own history, untouched by tab 2's navigation

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(a1))

    app.panes.a.tabs.select(1)
    wait_until(lambda: app.navigation.address_bar.text == str(b1))
    assert app.navigation.back.is_enabled()  # tab 2's own history, untouched by tab 1's back click

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == "This PC")
