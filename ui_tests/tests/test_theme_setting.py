"""Coverage for the Light/Dark theme setting (Architecture.md §14.50, SettingsDialog's General >
UI page) -- that it applies immediately and round-trips across a restart.

Like `test_hidden_files_toggle.py`'s "Show hidden files/folders" toggle, `UI/Theme` is a
`QSettings`-backed, genuinely app-wide preference (`UiTheme`), not scoped to `--data-dir` -- it
persists in the real developer machine's settings across every `ExpLorerApp` instance. Every test
here therefore discovers whatever the ambient theme already is and restores it afterward in a
`finally`, rather than assuming a known starting value, so this suite neither depends on nor
permanently flips the setting on whatever machine runs it.
"""

from __future__ import annotations

from framework import ExpLorerFreshAppTestCase, ExpLorerRestartableTestCase, open_settings_dialog
from framework.wait import wait_until


def _other_theme(theme: str) -> str:
    return "Dark" if theme == "Light" else "Light"


class TestThemeSettingAppliesImmediately(ExpLorerFreshAppTestCase):
    def test_switching_theme_updates_the_combo_box_immediately(self) -> None:
        dialog = open_settings_dialog(self.app)
        try:
            dialog.select_category("UI")
            before = wait_until(lambda: dialog.current_theme() or None)
            other = _other_theme(before)

            dialog.select_theme(other)
            after = wait_until(lambda: dialog.current_theme() if dialog.current_theme() != before else None)
            assert after == other
        finally:
            # Restore whatever the ambient theme was before this test touched it.
            if dialog.current_theme() != before:
                dialog.select_theme(before)
                wait_until(lambda: dialog.current_theme() == before or None)
            dialog.close()


class TestThemeSettingPersistsAcrossRestart(ExpLorerRestartableTestCase):
    def test_theme_round_trips_after_a_restart(self) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("UI")
        before = wait_until(lambda: dialog.current_theme() or None)
        other = _other_theme(before)
        try:
            dialog.select_theme(other)
            wait_until(lambda: dialog.current_theme() == other or None)
            dialog.close()

            self.restart()

            # No re-selection here: a QSettings-backed preference is read straight from disk at
            # startup, so the flipped value must already be in effect on the fresh instance.
            dialog = open_settings_dialog(self.app)
            dialog.select_category("UI")
            assert wait_until(lambda: dialog.current_theme() or None) == other
        finally:
            if dialog.current_theme() != before:
                dialog.select_theme(before)
                wait_until(lambda: dialog.current_theme() == before or None)
            dialog.close()
