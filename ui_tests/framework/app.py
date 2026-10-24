"""`ExpLorerApp`: the framework's single entry point for driving a running Exp-LORer instance.

Exposes the UI as typed nested page objects -- `app.navigation.back.click()`,
`app.right_panel.monitoring.start_monitoring.click()`, `app.layout.four_grid.click()` -- built
from plain classes/properties (see `element.py`) rather than a generic attribute-forwarding
proxy, so IDE intellisense can autocomplete and type-check every step of the chain instead of
guessing at pywinauto's dynamic API. `.window` is the deliberate escape hatch to the raw
pywinauto WindowSpecification for anything the page objects don't cover yet.
"""

from __future__ import annotations

import os
import re
import subprocess
import time
from pathlib import Path
from typing import Any, Optional, Union

from pywinauto import Application

from .automation_id import AutomationID
from .element import Element
from .favorites_menu import FavoritesMenu
from .layout import LayoutToolbar
from .left_panel import LeftPanel
from .notification_overlay import NotificationOverlay
from .pane import PaneNavigation, WorkspacePanes
from .right_panel import RightPanel
from .status_bar import MainStatusBar
from .view_menu import ViewMenu

WINDOW_TITLE = "Exp-LORer"
# MainWindow sets its title to "Exp-LORer — <workspace name>" (MainWindow.cpp's constructor)
# since the workspaces feature shipped -- match by prefix since the workspace name (e.g.
# "Default") is no longer a fixed string we can match exactly.
WINDOW_TITLE_RE = f"^{re.escape(WINDOW_TITLE)}( —.*)?$"
PROCESS_NAME = "Exp-LORer.exe"
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_EXE = REPO_ROOT / "build" / "Release" / "src" / "app" / PROCESS_NAME


class ExpLorerApp:
    """Drives one running Exp-LORer instance.

    Build one with `ExpLorerApp.start(data_dir=...)` -- every instance needs its own directory
    (Architecture.md §9's `--data-dir`) so tests never read/mutate a developer's real
    `%LOCALAPPDATA%/Exp-LORer/`, or collide with each other's config/tag state. The `app` fixture
    in `conftest.py` covers the common case (a fresh pytest `tmp_path` per test); call `.start()`
    directly instead when a test needs a specific/shared/pre-populated directory.
    """

    def __init__(self, application: Application, window: Any):
        self._application = application
        self.window = window  # raw pywinauto WindowSpecification escape hatch
        self.panes = WorkspacePanes(self._container)
        self.right_panel = RightPanel(self._container)
        self.left_panel = LeftPanel(self._container)
        self.layout = LayoutToolbar(self._container)
        self.status_bar = MainStatusBar(self._container)
        self.favorites = FavoritesMenu(self)
        self.view_menu = ViewMenu(self)
        self.advanced_search_button = Element(self._container, AutomationID.ADVANCED_SEARCH_BUTTON)
        self.notifications = NotificationOverlay(self._container)

    def _container(self) -> Any:
        return self.window

    @property
    def navigation(self) -> PaneNavigation:
        """Shortcut for `panes.a.navigation` -- pane A's toolbar, the one that matters in the
        default single-pane layout most tests run in. Use `panes.b/c/d.navigation` once a
        multi-pane layout (see `.layout`) is active.
        """
        return self.panes.a.navigation

    @classmethod
    def start(
        cls,
        data_dir: Union[str, Path],
        exe_path: Optional[Union[str, Path]] = None,
        timeout: float = 10,
    ) -> "ExpLorerApp":
        """Launch a fresh Exp-LORer instance pointed at `data_dir`.

        `exe_path` defaults to the `EXP_LORER_EXE` env var, then the standard
        `build/Release/src/app/Exp-LORer.exe` output. Raises `FileNotFoundError` if nothing is
        built yet.
        """
        resolved_exe = Path(exe_path) if exe_path else Path(os.environ.get("EXP_LORER_EXE", DEFAULT_EXE))
        if not resolved_exe.is_file():
            raise FileNotFoundError(
                f"Exp-LORer executable not found at {resolved_exe}. Build it first: .\\build.ps1 "
                "(or pass exe_path=..., or set the EXP_LORER_EXE env var)."
            )

        # wait_for_idle=False: WaitForInputIdle can raise ERROR_NOT_GUI_PROCESS (1471) if called
        # before the Qt process has finished initializing its message queue; we wait for the
        # window instead.
        application = Application(backend="uia").start(
            f'"{resolved_exe}" --data-dir "{data_dir}"', wait_for_idle=False
        )
        window = application.window(title_re=WINDOW_TITLE_RE)
        window.wait("visible", timeout=timeout)

        # Force the new window to the OS foreground. Confirmed by hand that without this,
        # double-click/Enter-based activation (QAbstractItemView::activated -- double_click_row,
        # activate_selection_with_enter, SearchPanel.double_click, ...) can silently never fire at
        # all in this environment even though a plain single click still lands fine: a freshly
        # `Application.start()`-launched window isn't guaranteed to actually be the foreground
        # window the moment it becomes visible, and double-click *registration* (unlike a
        # SelectionItem-pattern single click) depends on the real OS input focus/activation state.
        try:
            import win32gui

            win32gui.SetForegroundWindow(window.handle)
        except Exception:  # noqa: BLE001 -- best-effort; a failure here shouldn't abort startup
            pass

        return cls(application, window)

    def close(self) -> None:
        """Close the window, then kill the process if it's still around. Safe to call more than
        once, and safe even if the process already exited on its own.
        """
        try:
            if self._application.is_process_running():
                self.window.close()
        except Exception:
            pass
        try:
            if self._application.is_process_running():
                self._application.kill()
        except Exception:
            pass

    def __enter__(self) -> "ExpLorerApp":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    # -- thin, explicit passthroughs for the handful of top-level-window checks nearly every
    #    test wants; use `.window` directly for anything else pywinauto's WindowSpecification
    #    offers, rather than growing this list indefinitely --
    def exists(self) -> bool:
        return self.window.exists()

    def is_visible(self) -> bool:
        return self.window.is_visible()

    @property
    def title(self) -> str:
        return self.window.window_text()

    @staticmethod
    def kill_stray_instances() -> None:
        """Force-kill any leftover Exp-LORer.exe from a previous crashed/hung test run."""
        subprocess.run(["taskkill", "/IM", PROCESS_NAME, "/F"], capture_output=True)
        time.sleep(0.5)
