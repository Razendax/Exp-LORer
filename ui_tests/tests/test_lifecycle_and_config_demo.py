"""Demonstrates `ExpLorerTestCase`, the framework's base class for class-scoped tests -- one
Exp-LORer instance is started once (`setup_class`) and shared across every test method in the
class, instead of the `app` fixture's fresh-instance-per-test. `configure_workspace` seeds the
config.json the shared instance boots from (here, a two-pane `TwoVertical` layout with both panes
on "This PC"); `setup_method` below is a plain pytest xunit-style hook [1], not something
`ExpLorerTestCase` provides itself.

Not meant as coverage of app behavior; see other files under `tests/` for that.

[1] https://docs.pytest.org/en/stable/how-to/xunit_setup.html
"""

from __future__ import annotations

from framework import ExpLorerConfig, ExpLorerTestCase, SplitLayout


class TestLifecycleAndConfigDemo(ExpLorerTestCase):
    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        config.workspace.panes.a.add_tab()  # defaults to THIS_PC
        config.workspace.panes.b.add_tab()

    def setup_method(self) -> None:
        """Runs before each test method -- puts the shared instance back on the layout
        `configure_workspace` configured it with, in case a previous test method changed it.
        """
        self.app.layout.two_vertical.click()

    def test_starts_in_two_vertical_layout_from_config(self) -> None:
        assert self.app.panes.a.file_browser.exists()
        assert self.app.panes.b.file_browser.exists()
        assert not self.app.panes.c.file_browser.exists()
        assert self.app.panes.a.navigation.address_bar.text == "This PC"
        assert self.app.panes.b.navigation.address_bar.text == "This PC"

    def test_layout_can_change_and_setup_method_resets_it_next_time(self) -> None:
        self.app.layout.four_grid.click()
        assert self.app.panes.c.file_browser.exists()
        assert self.app.panes.d.file_browser.exists()
        # test_starts_in_two_vertical_layout_from_config passing regardless of test order proves
        # setup_method actually ran and put the layout back.
