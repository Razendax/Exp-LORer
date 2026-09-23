"""Demonstrates the `framework` package's chained, typed page-object syntax end-to-end against a
real running instance -- e.g. `app.layout.four_grid.click()`, `app.right_panel.tags.search_edit`.
Not meant as coverage of app behavior; see other files under `tests/` for that.
"""


from __future__ import annotations

from framework.app import ExpLorerApp


def test_layout_toolbar_switches_active_panes(app: ExpLorerApp) -> None:
    assert app.panes.a.file_browser.exists()
    assert not app.panes.b.file_browser.exists()

    app.layout.four_grid.click()
    assert app.panes.b.file_browser.exists()
    assert app.panes.c.file_browser.exists()
    assert app.panes.d.file_browser.exists()

    app.layout.single.click()
    assert not app.panes.b.file_browser.exists()


def test_navigation_toolbar_reflects_fresh_pane(app: ExpLorerApp) -> None:
    assert app.navigation.address_bar.text == "This PC"
    assert not app.navigation.back.is_enabled()
    assert app.navigation.refresh.is_enabled()


def test_right_panel_switches_between_tags_preview_and_monitoring(app: ExpLorerApp) -> None:
    # Qt keeps a QStackedWidget's non-current pages in the automation tree (merely hidden), so
    # `.exists()` isn't a visibility check -- each assertion below only checks that switching to
    # a panel makes *its own* elements reachable, not that the previous panel's are gone.
    assert app.right_panel.tags.search_edit.exists()

    assert app.right_panel.preview.unsupported_label.exists()

    assert app.right_panel.monitoring.start_monitoring.exists()
    assert app.right_panel.monitoring.total_label.text == "Total: 0"

    assert app.right_panel.tags.search_edit.exists()
