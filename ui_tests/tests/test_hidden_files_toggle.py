"""Coverage for the View menu's "Show hidden files/folders" toggle (Architecture.md §14.22) --
`FileListModel`'s own hidden-row filter, independent of any dotfile convention (this suite seeds a
real Windows `FILE_ATTRIBUTE_HIDDEN` file via `attrib`, not a leading-dot name).

The toggle is a `QSettings`-backed, genuinely app-wide preference (not scoped to `--data-dir`, by
design -- Architecture.md §14.22's own "same posture as §14.13's shell-extensions toggle"),
confirmed by hand: it persists in the real developer machine's settings across every
`ExpLorerApp` instance, not just within one test's data directory. Every test here therefore
discovers whatever the ambient state already is and restores it afterward in a `finally`, rather
than assuming a known starting value -- both to stay correct regardless of what a previous local
run (by this suite, or by a developer's own manual use of the app) left behind, and to not
permanently flip the setting on whatever machine runs this suite.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from framework import ExpLorerFreshAppTestCase, ExpLorerRestartableTestCase
from framework.details_view import read_details_rows
from framework.wait import wait_until


def _hidden_file_visible(app, target: Path) -> bool:
    app.navigation.navigate_to(str(target))
    rows = read_details_rows(app.panes.a.file_tree_view)
    return any(row.name == "secret.txt" for row in rows)


def _make_hidden_file(target: Path) -> None:
    target.mkdir()
    hidden = target / "secret.txt"
    hidden.write_text("x")
    subprocess.run(["attrib", "+h", str(hidden)], check=True)


class TestHiddenFilesToggle(ExpLorerFreshAppTestCase):
    def test_toggle_flips_hidden_file_visibility(self, tmp_path: Path) -> None:
        target = tmp_path / "target"
        _make_hidden_file(target)

        before = _hidden_file_visible(self.app, target)
        try:
            self.app.view_menu.toggle_show_hidden_files()
            wait_until(lambda: _hidden_file_visible(self.app, target) != before or None)
            assert _hidden_file_visible(self.app, target) != before
        finally:
            # Restore whatever the ambient state was before this test touched it.
            if _hidden_file_visible(self.app, target) != before:
                self.app.view_menu.toggle_show_hidden_files()
                wait_until(lambda: _hidden_file_visible(self.app, target) == before or None)


class TestHiddenFilesTogglePersistsAcrossRestart(ExpLorerRestartableTestCase):
    def test_setting_persists_across_a_restart(self, tmp_path: Path) -> None:
        target = tmp_path / "target"
        _make_hidden_file(target)

        before = _hidden_file_visible(self.app, target)
        try:
            self.app.view_menu.toggle_show_hidden_files()
            after_toggle = wait_until(lambda: _hidden_file_visible(self.app, target) != before or None)
            assert after_toggle

            self.restart()
            # No re-toggle here: a QSettings-backed preference is read straight from disk at
            # startup, so the flipped value must already be in effect on the fresh instance.
            assert _hidden_file_visible(self.app, target) != before
        finally:
            if _hidden_file_visible(self.app, target) != before:
                self.app.view_menu.toggle_show_hidden_files()
                wait_until(lambda: _hidden_file_visible(self.app, target) == before or None)
