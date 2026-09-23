"""`ExpLorerTestCase`: base class for grouping UI tests that share one Exp-LORer instance across
an entire test class, using pytest's native xunit-style `setup_class`/`teardown_class`/
`setup_method`/`teardown_method` hooks [1] -- a plain class is enough, no `unittest.TestCase`
needed, and subclasses are free to add their own `setup_method`/`teardown_method` on top of what
this class provides.

Prefer the `app`/`start_app` fixtures in conftest.py (a fresh instance per test) unless a test
class specifically needs to amortize the ~1s app-launch cost across many test methods.

Usage:

    class TestSomething(ExpLorerTestCase):
        @classmethod
        def configure_workspace(cls, config: ExpLorerConfig) -> None:
            config.workspace.layout = SplitLayout.TWO_VERTICAL
            config.workspace.panes.a.add_tab()
            config.workspace.panes.b.add_tab()

        def setup_method(self):
            self.app.layout.two_vertical.click()  # reset any state a previous test method left

        def test_something(self):
            assert self.app.panes.a.file_browser.exists()

`setup_class`/`teardown_class` are provided by `ExpLorerTestCase` itself and start/stop the
shared `self.app` (an `ExpLorerApp`); override `configure_workspace` to customize the config.json
it boots from (defaults to no-op, i.e. Exp-LORer's own defaults -- single pane, This PC).

[1] https://docs.pytest.org/en/stable/how-to/xunit_setup.html
"""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path

import pytest

from .app import ExpLorerApp
from .config import ExpLorerConfig


class ExpLorerTestCase:
    app: ExpLorerApp

    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig) -> None:
        """Override to customize the config.json the shared instance boots from -- e.g. set
        `config.workspace.layout` or add tabs via `config.workspace.panes`. No-op by default.
        """

    @classmethod
    def setup_class(cls) -> None:
        """Runs once before any test method in this class: writes a config.json (via
        `configure_workspace`) and starts a single Exp-LORer instance that every test method in
        the class shares. Skips the whole class if the app isn't built yet.
        """
        cls._tmp_dir = tempfile.mkdtemp(prefix="explorer_uitest_")
        data_dir = Path(cls._tmp_dir) / "data"

        config = ExpLorerConfig(data_dir)
        cls.configure_workspace(config)
        config.workspace.save()

        ExpLorerApp.kill_stray_instances()
        try:
            cls.app = ExpLorerApp.start(data_dir=data_dir)
        except FileNotFoundError as exc:
            shutil.rmtree(cls._tmp_dir, ignore_errors=True)
            pytest.skip(str(exc))

    @classmethod
    def teardown_class(cls) -> None:
        """Runs once after every test method in this class has finished: closes the shared
        instance and removes its data directory.
        """
        cls.app.close()
        shutil.rmtree(cls._tmp_dir, ignore_errors=True)
