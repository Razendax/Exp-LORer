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
from collections.abc import Iterator
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


class ExpLorerFreshAppTestCase:
    """Base for a test class that needs its own fresh Exp-LORer instance per test method rather
    than one shared across the class (see `ExpLorerTestCase`'s docstring for when a shared
    instance is fine) -- most useful when a test's assertions depend on exact *global* state (e.g.
    TagManagerDialog's tag list, which isn't scoped to any one folder/tmp_path) that a shared
    instance would carry over from a previous test method in the same class.

    Starting a fresh instance needs `tmp_path` (for a private data directory), which
    `setup_method`/`teardown_method` can't receive -- only an autouse fixture can -- so this uses
    one instead of `ExpLorerTestCase`'s classmethod hooks.

        class TestSomething(ExpLorerFreshAppTestCase):
            def test_something(self) -> None:
                assert self.app.panes.a.file_browser.exists()
    """

    app: ExpLorerApp

    @pytest.fixture(autouse=True)
    def _fresh_app(self, tmp_path: Path) -> Iterator[None]:
        ExpLorerApp.kill_stray_instances()
        with ExpLorerApp.start(data_dir=tmp_path / "data") as app:
            self.app = app
            yield


class ExpLorerRestartableTestCase:
    """Base for tests that need to close and relaunch Exp-LORer against the SAME data_dir, to
    verify state round-trips through a real close -> relaunch cycle rather than just landing in
    config.json correctly. Needed for anything captured in `MainWindow::closeEvent` and restored
    only once, in the constructor (Architecture.md §14.14) -- e.g. side-panel expand/tab/width
    persistence (§14.48) -- where the thing actually under test is the save-on-close/restore-on-
    launch wiring itself, not the JSON shape (that's covered at the adapter level by
    AppConfigStoreTest).

    Starts one instance per test method (fresh `tmp_path`, stored as `self.data_dir`) in
    `self.app`; call `self.restart()` to close it and launch a fresh instance against the same
    `data_dir`, replacing `self.app` with the new one:

        class TestSomething(ExpLorerRestartableTestCase):
            def test_something(self) -> None:
                self.app.layout.two_vertical.click()
                self.restart()
                assert self.app.layout.two_vertical.is_checked()
    """

    app: ExpLorerApp
    data_dir: Path

    @pytest.fixture(autouse=True)
    def _fresh_app(self, tmp_path: Path) -> Iterator[None]:
        ExpLorerApp.kill_stray_instances()
        self.data_dir = tmp_path / "data"
        self.app = ExpLorerApp.start(data_dir=self.data_dir)
        try:
            yield
        finally:
            self.app.close()

    def restart(self) -> None:
        """Closes the current instance (saving config.json via its closeEvent) and starts a fresh
        one against the same data_dir, which boots by reading back whatever the previous instance
        just saved.
        """
        self.app.close()
        self.app = ExpLorerApp.start(data_dir=self.data_dir)
