"""Coverage for opening a file with its OS-registered default application
(`FileOperationsController::openFile` -> `FileNavigationUseCase::openFile`, Architecture.md
§14.11) via double-click and Enter-to-open, plus the "a folder navigates instead of launching
anything" distinction (`FileBrowserView::itemActivated`'s `isDirectory` branch).

Deliberately environment-dependent: it relies on `.txt` being registered to a text editor (true
on every stock Windows install -- Notepad), and keeps assertions loose (some new top-level window
appeared) rather than asserting on any specific editor's own UI, which this suite has no page
object for and shouldn't grow one just for this.

The two file-opening tests below could not be verified passing in this repo's own sandboxed dev
session: `ShellExecuteW`'s "open" verb (what `openFile` calls) never produced a visible window
there at all, confirmed independent of this app (`os.startfile` and PowerShell's `Start-Process`
on the same file also did nothing), and instead leaves a lingering `ApplicationFrameHost.exe`
behind each time -- apparently that sandbox's file-association broker hangs rather than resolving
or failing cleanly. A real interactive Windows session shouldn't hit this; if these two tests
hang/fail in CI or another sandboxed runner, suspect the same cause before suspecting `openFile`
itself, and kill any stray `ApplicationFrameHost.exe` it leaves behind.
"""

from __future__ import annotations

import time
from pathlib import Path

import win32con
import win32gui

from framework.app import ExpLorerApp
from framework.details_view import activate_selection_with_enter, double_click_row, select_row
from framework.modal_dialog import visible_top_level_window_handles, wait_for_new_top_level_window
from framework.wait import wait_until


def _close_window(hwnd: int) -> None:
    """Best-effort cleanup for an externally-launched window (e.g. Notepad) this test spawned."""
    try:
        win32gui.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)
    except Exception:  # noqa: BLE001 -- cleanup only, never fail the test over it
        pass


def test_double_click_on_a_file_opens_exactly_one_new_window(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "notes.txt"
    target.write_text("hello")
    app.navigation.navigate_to(str(tmp_path))

    before = visible_top_level_window_handles()
    double_click_row(app.panes.a.file_tree_view, "notes.txt")

    new_hwnd = wait_for_new_top_level_window(before, timeout=10)
    try:
        assert win32gui.IsWindow(new_hwnd)
    finally:
        _close_window(new_hwnd)


def test_enter_on_a_selected_file_opens_exactly_one_new_window(app: ExpLorerApp, tmp_path: Path) -> None:
    target = tmp_path / "notes.txt"
    target.write_text("hello")
    app.navigation.navigate_to(str(tmp_path))

    tree = app.panes.a.file_tree_view
    select_row(tree, "notes.txt")

    before = visible_top_level_window_handles()
    activate_selection_with_enter(tree)

    new_hwnd = wait_for_new_top_level_window(before, timeout=10)
    try:
        assert win32gui.IsWindow(new_hwnd)
    finally:
        _close_window(new_hwnd)


def test_double_click_on_a_folder_navigates_without_launching_anything(app: ExpLorerApp, tmp_path: Path) -> None:
    sub = tmp_path / "subfolder"
    sub.mkdir()
    app.navigation.navigate_to(str(tmp_path))

    before = visible_top_level_window_handles()
    double_click_row(app.panes.a.file_tree_view, "subfolder")

    wait_until(lambda: app.navigation.address_bar.text == str(sub))
    # Give any stray external launch a moment to appear before asserting none did.
    time.sleep(1)
    assert visible_top_level_window_handles() == before
