"""Coverage for left/right side-panel expand/active-tab/width persistence across restarts
(Architecture.md §14.48): `AppConfig.leftPanel`/`rightPanel`, captured in `MainWindow::closeEvent`
and restored once in the `MainWindow` constructor.

The JSON shape itself (save/load round-trip, tolerant defaults) is already covered at the adapter
level by `AppConfigStoreTest` (tests/adapters/AppConfigStoreTest.cpp). What that suite can't cover
is the actual save-on-close/restore-on-launch *wiring* -- `SidePanelWidget::activateTab`/
`setPreferredExpandedWidth`, the splitter resize it drives, and `MainWindow` reading the live
splitter sizes back in `closeEvent` -- since that's UI-layer and not part of CTest (Architecture.md
§11). This suite exercises it through a real close -> relaunch cycle via `ExpLorerRestartableTestCase`.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

from framework import ExpLorerConfig, ExpLorerRestartableTestCase
from framework.wait import wait_until


def _load_config(data_dir: Path) -> dict[str, Any]:
    return json.loads(ExpLorerConfig(data_dir).path.read_text(encoding="utf-8"))


class TestSidePanelPersistsAcrossRestart(ExpLorerRestartableTestCase):
    def test_left_panel_search_tab_and_width_restore_after_restart(self) -> None:
        self.app.left_panel.search  # expands the left panel onto its Search tab
        wait_until(lambda: self.app.left_panel.search_button.is_checked())

        self.restart()

        saved = _load_config(self.data_dir)
        assert saved["leftPanel"]["expanded"] is True
        assert saved["leftPanel"]["activeTabIndex"] == 1  # Tree=0, Search=1 (registration order)
        assert saved["leftPanel"]["width"] > 0

        assert self.app.left_panel.search_button.is_checked()
        assert self.app.left_panel.is_expanded()

    def test_right_panel_preview_tab_and_width_restore_after_restart(self) -> None:
        self.app.right_panel.preview  # expands the right panel onto its Preview tab
        wait_until(lambda: self.app.right_panel.preview_button.is_checked())

        self.restart()

        saved = _load_config(self.data_dir)
        assert saved["rightPanel"]["expanded"] is True
        assert saved["rightPanel"]["activeTabIndex"] == 1  # Tags=0, Preview=1, Monitoring=2
        assert saved["rightPanel"]["width"] > 0

        assert self.app.right_panel.preview_button.is_checked()
        assert self.app.right_panel.is_expanded()

    def test_collapsed_panels_stay_collapsed_after_restart(self) -> None:
        assert not self.app.left_panel.is_expanded()
        assert not self.app.right_panel.is_expanded()

        self.restart()

        saved = _load_config(self.data_dir)
        assert saved["leftPanel"]["expanded"] is False
        assert saved["rightPanel"]["expanded"] is False
        assert not self.app.left_panel.is_expanded()
        assert not self.app.right_panel.is_expanded()

    def test_expanding_then_collapsing_persists_collapsed_with_unset_width(self) -> None:
        self.app.left_panel.tree  # expand onto Tree
        wait_until(lambda: self.app.left_panel.tree_button.is_checked())

        self.app.left_panel.tree_button.click()  # clicking the active tab again collapses it
        wait_until(lambda: not self.app.left_panel.is_expanded())

        self.restart()

        saved = _load_config(self.data_dir)
        assert saved["leftPanel"]["expanded"] is False
        # 0 is the "unset" sentinel (AppConfig.h) -- a panel collapsed at close time has no
        # meaningful "last expanded width" to preserve.
        assert saved["leftPanel"]["width"] == 0
        assert not self.app.left_panel.is_expanded()

    def test_width_stays_stable_across_multiple_restarts(self) -> None:
        self.app.left_panel.search
        wait_until(lambda: self.app.left_panel.search_button.is_checked())

        self.restart()
        width_after_first_restart = _load_config(self.data_dir)["leftPanel"]["width"]

        self.restart()  # nothing touched this cycle -- the restored width must not drift
        width_after_second_restart = _load_config(self.data_dir)["leftPanel"]["width"]

        assert width_after_second_restart == width_after_first_restart
        assert self.app.left_panel.search_button.is_checked()

    def test_collapsed_panels_stay_collapsed_after_maximizing_before_close(self) -> None:
        """Regression test: SidePanelWidget::resizeEvent's "dragging the splitter handle out
        reveals the panel" convenience logic used to fire on ANY resize that pushed a collapsed
        panel's width past its collapsed width -- including one caused by the *window* being
        maximized (QSplitter proportionally redistributes space across its children), not just an
        actual handle drag. That silently re-expanded both panels right before close, which then
        got persisted -- so a user who explicitly collapsed both panels and maximized the window
        before exiting would find them back open on next launch. Fixed by only treating it as a
        drag when the splitter's own total width is unchanged across the resize.
        """
        self.app.left_panel.tree
        wait_until(lambda: self.app.left_panel.tree_button.is_checked())
        self.app.right_panel.tags
        wait_until(lambda: self.app.right_panel.tags_button.is_checked())

        self.app.left_panel.tree_button.click()  # collapse
        self.app.right_panel.tags_button.click()  # collapse
        wait_until(lambda: not self.app.left_panel.is_expanded())
        wait_until(lambda: not self.app.right_panel.is_expanded())

        self.app.window.maximize()
        time.sleep(0.5)  # let the resulting resize/layout settle
        assert not self.app.left_panel.is_expanded()
        assert not self.app.right_panel.is_expanded()

        self.restart()

        saved = _load_config(self.data_dir)
        assert saved["leftPanel"]["expanded"] is False
        assert saved["rightPanel"]["expanded"] is False
        assert not self.app.left_panel.is_expanded()
        assert not self.app.right_panel.is_expanded()
