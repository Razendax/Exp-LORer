"""Coverage for the per-pane Back/Forward/Up navigation controls (WorkspacePaneWidget's toolbar,
backed by NavigationHistory/TabViewModel::goBack/goForward/goUp and TabViewModel::upTarget) --
history traversal, a fresh navigation clearing forward history, and Up's parent-folder / This-PC
behavior.

Uses the `app` fixture (fresh Exp-LORer instance per test) rather than `ExpLorerTestCase` -- these
tests assert on exact enabled/disabled transitions of the nav buttons starting from a known-empty
history, which a shared instance carrying leftover history from a previous test method would make
fragile.
"""

from __future__ import annotations

from pathlib import Path

from framework.app import ExpLorerApp
from framework.wait import wait_until


def test_back_and_forward_disabled_with_no_history(app: ExpLorerApp) -> None:
    assert not app.navigation.back.is_enabled()
    assert not app.navigation.forward.is_enabled()


def test_back_and_forward_traverse_navigation_history(app: ExpLorerApp, tmp_path: Path) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    first.mkdir()
    second.mkdir()

    app.navigation.navigate_to(str(first))
    app.navigation.navigate_to(str(second))
    assert app.navigation.back.is_enabled()
    assert not app.navigation.forward.is_enabled()

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(first))
    assert app.navigation.forward.is_enabled()

    app.navigation.forward.click()
    wait_until(lambda: app.navigation.address_bar.text == str(second))
    assert not app.navigation.forward.is_enabled()


def test_navigating_after_going_back_clears_forward_history(app: ExpLorerApp, tmp_path: Path) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    third = tmp_path / "third"
    for directory in (first, second, third):
        directory.mkdir()

    app.navigation.navigate_to(str(first))
    app.navigation.navigate_to(str(second))
    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(first))
    assert app.navigation.forward.is_enabled()

    app.navigation.navigate_to(str(third))
    assert not app.navigation.forward.is_enabled()


def test_multiple_back_clicks_walk_all_the_way_back_to_this_pc(app: ExpLorerApp, tmp_path: Path) -> None:
    first = tmp_path / "first"
    second = tmp_path / "second"
    third = tmp_path / "third"
    for directory in (first, second, third):
        directory.mkdir()

    app.navigation.navigate_to(str(first))
    app.navigation.navigate_to(str(second))
    app.navigation.navigate_to(str(third))

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(second))
    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(first))
    assert app.navigation.back.is_enabled()  # the fresh app's initial "This PC" is still further back

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == "This PC")
    assert not app.navigation.back.is_enabled()


def test_up_disabled_at_this_pc_root(app: ExpLorerApp) -> None:
    assert app.navigation.address_bar.text == "This PC"
    assert not app.navigation.up.is_enabled()


def test_up_navigates_to_parent_folder(app: ExpLorerApp, tmp_path: Path) -> None:
    parent = tmp_path / "parent"
    child = parent / "child"
    child.mkdir(parents=True)

    app.navigation.navigate_to(str(child))
    assert app.navigation.up.is_enabled()

    app.navigation.up.click()
    wait_until(lambda: app.navigation.address_bar.text == str(parent))


def test_up_is_recorded_as_navigation_history(app: ExpLorerApp, tmp_path: Path) -> None:
    parent = tmp_path / "parent"
    child = parent / "child"
    child.mkdir(parents=True)

    app.navigation.navigate_to(str(child))
    app.navigation.up.click()
    wait_until(lambda: app.navigation.address_bar.text == str(parent))
    assert app.navigation.back.is_enabled()

    app.navigation.back.click()
    wait_until(lambda: app.navigation.address_bar.text == str(child))
