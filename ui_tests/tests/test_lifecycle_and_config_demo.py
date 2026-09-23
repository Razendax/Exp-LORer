"""Demonstrates two framework features together:

1. Class-scoped `setup_class`/`teardown_class` (run once for the whole class) plus per-test
   `setup_method`/`teardown_method` -- pytest supports these "xunit-style" hooks natively [1] on
   a plain class, no `unittest.TestCase` needed. Useful when starting Exp-LORer per test (the
   `app` fixture in `conftest.py`) is more than a test class needs -- here one instance is shared
   across every test method instead.
2. `ExpLorerConfig`, a fluent builder for a test-owned `config.json`, used here to boot that
   shared instance straight into a two-pane layout instead of driving the UI into that state by
   hand.

Not meant as coverage of app behavior; see other files under `tests/` for that.

[1] https://docs.pytest.org/en/stable/how-to/xunit_setup.html
"""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path

import pytest

from framework import ExpLorerApp, ExpLorerConfig, SplitLayout


class TestLifecycleAndConfigDemo:
    @classmethod
    def setup_class(cls):
        """Runs once before any test method in this class. Writes a config.json (via
        `ExpLorerConfig`) that puts pane A and pane B each on a "This PC" tab in a
        `TwoVertical` split, then starts a single Exp-LORer instance against it that every test
        method below shares.
        """
        cls._tmp_dir = tempfile.mkdtemp(prefix="explorer_lifecycle_demo_")
        data_dir = Path(cls._tmp_dir) / "data"

        config = ExpLorerConfig(data_dir)
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        config.workspace.panes.a.add_tab()  # defaults to THIS_PC
        config.workspace.panes.b.add_tab()
        config.workspace.save()

        ExpLorerApp.kill_stray_instances()
        try:
            cls.app = ExpLorerApp.start(data_dir=data_dir)
        except FileNotFoundError as exc:
            shutil.rmtree(cls._tmp_dir, ignore_errors=True)
            pytest.skip(str(exc))

    @classmethod
    def teardown_class(cls):
        """Runs once after every test method in this class has finished."""
        cls.app.close()
        shutil.rmtree(cls._tmp_dir, ignore_errors=True)

    def setup_method(self):
        """Runs before each test method -- puts the shared instance back on the layout
        `setup_class` configured it with, in case a previous test method changed it.
        """
        self.app.layout.two_vertical.click()

    def test_starts_in_two_vertical_layout_from_config(self):
        assert self.app.panes.a.file_browser.exists()
        assert self.app.panes.b.file_browser.exists()
        assert not self.app.panes.c.file_browser.exists()
        assert self.app.panes.a.navigation.address_bar.text == "This PC"
        assert self.app.panes.b.navigation.address_bar.text == "This PC"

    def test_layout_can_change_and_setup_method_resets_it_next_time(self):
        self.app.layout.four_grid.click()
        assert self.app.panes.c.file_browser.exists()
        assert self.app.panes.d.file_browser.exists()
        # test_starts_in_two_vertical_layout_from_config passing regardless of test order proves
        # setup_method actually ran and put the layout back.
