"""Coverage for Keyboard Navigation & Hotkey Customization (prev_spec.md/prev_plan.md under
`.claude/active_work/`) -- the 44-row (6-category) rebindable hotkey catalog
(`src/adapters/hotkeys/HotkeyCatalog.cpp`), its Settings > General > Hotkeys page
(`SettingsHotkeysPage.cpp`), and the two runtime dispatch paths (`FileBrowserView::handleKeyPress`
for View-scoped actions, `HotkeyWindowDispatcher` for Window-scoped ones).

Every rebind-affecting test uses `ExpLorerFreshAppTestCase`: `hotkeys.json` lives directly under
`--data-dir` (`tmp_path / "data" / "hotkeys.json"`), so a shared instance across test methods
would leak one test's rebinds into the next.

`framework/settings_hotkeys_page.py`'s module docstring documents a confirmed environment
limitation central to how these tests work: Qt's `uia` bridge never exposes a row's
`HotkeyEditWidget`/Clear/Reset buttons/conflict label as readable text -- only their on-screen
position, which `SettingsHotkeysPage` clicks through to drive them. Every assertion here therefore
verifies a rebind through its functional effect (does the new chord actually fire the action? does
the old one now do nothing?) or by reading `hotkeys.json` straight off disk, never by reading a
shortcut/conflict label's displayed text.
"""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path
from typing import Any, Optional

from pywinauto.keyboard import send_keys

from framework import (
    ExpLorerConfig,
    ExpLorerFreshAppTestCase,
    ExpLorerPreparedDataDirTestCase,
    ExpLorerRestartableTestCase,
    ExpLorerTestCase,
    SplitLayout,
    open_context_menu,
    open_settings_dialog,
    read_details_rows,
    row_item,
    select_row,
    selected_names,
    wait_until,
)

# -- small local helpers --


def _hotkeys_json_path(tmp_path: Path) -> Path:
    return tmp_path / "data" / "hotkeys.json"


def _read_hotkeys_json(tmp_path: Path) -> Optional[dict]:
    path = _hotkeys_json_path(tmp_path)
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def _override_for(data: Optional[dict], stable_id: str) -> Optional[dict]:
    if data is None:
        return None
    for entry in data.get("overrides", []):
        if entry.get("id") == stable_id:
            return entry
    return None


def _make_hidden_file(target: Path) -> None:
    target.mkdir()
    hidden = target / "secret.txt"
    hidden.write_text("x")
    subprocess.run(["attrib", "+h", str(hidden)], check=True)


def _hidden_file_visible(app: Any, target: Path) -> bool:
    app.navigation.navigate_to(str(target))
    rows = read_details_rows(app.panes.a.file_tree_view)
    return any(row.name == "secret.txt" for row in rows)


# Per-category row counts recounted by hand against HotkeyCatalog.cpp's actual table (NOT the
# "That's 38 actions" claim in prev_spec.md §3, which is stale against its own per-category
# tables -- 5+4+10+8+7+10 = 44, matching HotkeyAction.h's 44 enumerators exactly).
_EXPECTED_ROW_COUNTS = {
    "Navigation": 5,
    "Tabs": 4,
    "Panes & Layout": 10,
    "Panels": 8,
    "Search & Tools": 7,
    "File Operations & Selection": 10,
}


# ===================== Tier 1: framework and Settings UI itself =====================


class TestHotkeysSettingsPageStructure(ExpLorerFreshAppTestCase):
    def test_all_six_categories_present_with_expected_row_counts(self) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        by_category = dialog.hotkeys.rows_by_category_full()

        assert set(by_category.keys()) == set(_EXPECTED_ROW_COUNTS.keys())
        for category, expected_count in _EXPECTED_ROW_COUNTS.items():
            assert len(by_category[category]) == expected_count, (
                f"{category}: expected {expected_count} rows, got {by_category[category]}"
            )
        assert sum(len(rows) for rows in by_category.values()) == 44

        assert "Copy" in by_category["File Operations & Selection"]
        assert "Navigate back" in by_category["Navigation"]
        assert "New tab" in by_category["Tabs"]

    def test_filter_narrows_rows_by_name_case_insensitive(self) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.filter_actions("copy")

        assert dialog.hotkeys.visible_row_names() == ["Copy"]

    def test_filter_hides_category_with_no_matching_rows(self) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.filter_actions("Favorites")

        assert dialog.hotkeys.visible_category_names() == ["Search & Tools"]
        assert dialog.hotkeys.visible_row_names() == ["Add selection to Favorites"]


class TestHotkeyRecording(ExpLorerFreshAppTestCase):
    def test_escape_while_listening_cancels_without_change(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.cancel_listening("Navigate forward")

        assert _read_hotkeys_json(tmp_path) is None

    def test_recording_a_new_chord_updates_binding_and_persists(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("Navigate back", "^%9")  # Ctrl+Alt+9

        data = _read_hotkeys_json(tmp_path)
        override = _override_for(data, "nav.back")
        assert override is not None
        assert override["chord"] == "Ctrl+Alt+9"

    def test_bare_key_rejected_when_action_disallows_it(self, tmp_path: Path) -> None:
        """"Copy" (fileOps.copy) has `allowsBareRebind=false` -- a bare, no-modifier key press
        while listening must be rejected (ignored, binding untouched), not accepted.
        """
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.click_shortcut("Copy")
        dialog.hotkeys.send_chord("k")
        time.sleep(0.3)
        assert _read_hotkeys_json(tmp_path) is None
        dialog.hotkeys.send_chord("{ESC}")  # leave listening mode cleanly

    def test_bare_key_allowed_when_action_permits_it(self, tmp_path: Path) -> None:
        """"Rename" (fileOps.rename) is one of the 8 actions with `allowsBareRebind=true`
        (Delete/DeletePermanently/Rename/NavigateToParent/OpenOrNavigateInto/Properties/
        CycleNextPane/CyclePreviousPane -- confirmed against HotkeyCatalog.cpp) -- a bare key must
        be accepted outright.
        """
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("Rename", "k")

        data = _read_hotkeys_json(tmp_path)
        override = _override_for(data, "fileOps.rename")
        assert override is not None
        assert override["chord"] == "K"


class TestHotkeyConflictWarnButAllow(ExpLorerFreshAppTestCase):
    def test_rebinding_to_an_in_use_chord_keeps_both_bindings(self, tmp_path: Path) -> None:
        """Spec §4/§5's "warn but allow": entering a chord already bound (same scope) to a
        different action must NOT revert either row -- both keep the chord. The inline warning
        label itself is unreadable via UI Automation (see module docstring); this verifies the
        "allow" half (both bindings actually stick) via `hotkeys.json`, and that the second bind is
        recorded with a strictly higher `order` (spec §5's recency tie-break).
        """
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("Navigate back", "^%6")
        dialog.hotkeys.record_chord("Navigate forward", "^%6")

        data = _read_hotkeys_json(tmp_path)
        back = _override_for(data, "nav.back")
        forward = _override_for(data, "nav.forward")
        assert back is not None and forward is not None
        assert back["chord"] == "Ctrl+Alt+6"
        assert forward["chord"] == "Ctrl+Alt+6"
        assert forward["order"] > back["order"]

    def test_rebinding_to_a_chord_already_live_on_a_window_scoped_action_still_records(
        self, tmp_path: Path
    ) -> None:
        """Same "warn but allow" promise as above, but with the ALREADY-bound action being
        Window-scoped ("New tab", live and firing regardless of focus) rather than View-scoped.

        CONFIRMED BY HAND (see this session's final report) that this currently fails: typing the
        candidate chord into "Close tab"'s listening `HotkeyEditWidget` never reaches it at all --
        the globally-installed `HotkeyWindowDispatcher` intercepts the keystroke first (since
        "New tab" already resolves to it) and fires "New tab" for real (a tab silently opens)
        instead of letting the Settings page's recording widget see it as a candidate chord. No
        override for "Close tab" is ever written. This test asserts the behavior spec §4/§5
        promises, not the current (buggy) one -- it is expected to fail until that's fixed.
        """
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("New tab", "^%7")
        dialog.hotkeys.record_chord("Close tab", "^%7")

        data = _read_hotkeys_json(tmp_path)
        new_tab = _override_for(data, "tabs.new")
        close_tab = _override_for(data, "tabs.close")
        assert new_tab is not None and new_tab["chord"] == "Ctrl+Alt+7"
        assert close_tab is not None, (
            "'Close tab' should have been rebound to Ctrl+Alt+7 same as 'New tab' -- instead the "
            "keystroke was hijacked by the live HotkeyWindowDispatcher binding for 'New tab' and "
            "no override was ever recorded for 'Close tab'"
        )
        assert close_tab["chord"] == "Ctrl+Alt+7"


class TestHotkeyClear(ExpLorerFreshAppTestCase):
    def test_clearing_rename_disables_f2_but_not_context_menu_rename(self, tmp_path: Path) -> None:
        target_dir = tmp_path / "target"
        target_dir.mkdir()
        (target_dir / "child").mkdir()
        # A FILE, not a second folder, for the context-menu half below -- see this session's final
        # report for a separate, confirmed production bug where the inline rename editor never
        # receives any keystrokes for a FOLDER target specifically (file targets are unaffected,
        # which this suite's own test_context_menu.py already exercises and passes).
        (target_dir / "sibling.txt").write_text("x")
        self.app.navigation.navigate_to(str(target_dir))

        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")
        dialog.hotkeys.clear("Rename")
        dialog.close()

        tree_view = self.app.panes.a.file_tree_view
        select_row(tree_view, "child")
        tree_view.raw.set_focus()
        send_keys("{F2}")
        time.sleep(0.3)
        # If F2 still entered rename mode, this would replace "child"'s name with "nope". If F2 is
        # truly unreachable (Clear worked), these keystrokes fall through to plain keyboardSearch
        # (a non-goal, spec §6, left untouched) and Enter instead activates the still-selected
        # "child" folder, navigating into it -- a safe, unambiguous, distinguishing signal that
        # doesn't risk launching an external default-app process (unlike doing this on a file).
        send_keys("nope{ENTER}")
        wait_until(lambda: self.app.navigation.address_bar.text == str(target_dir / "child"))
        assert (target_dir / "child").exists()
        assert not (target_dir / "nope").exists()

        self.app.navigation.up.click()
        wait_until(lambda: self.app.navigation.address_bar.text == str(target_dir))

        # Context-menu Rename must still work (spec §5: Clear removes keyboard reachability only).
        select_row(tree_view, "sibling.txt")
        item = row_item(tree_view, "sibling.txt")
        menu = open_context_menu(item)
        menu.click("Rename")
        time.sleep(0.3)
        send_keys("^a" + "renamed.txt" + "{ENTER}", with_spaces=True)
        wait_until(lambda: (target_dir / "renamed.txt").exists())
        assert not (target_dir / "sibling.txt").exists()


class TestHotkeyReset(ExpLorerFreshAppTestCase):
    def test_reset_restores_catalog_default(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("Navigate back", "^%9")
        assert _override_for(_read_hotkeys_json(tmp_path), "nav.back") is not None

        dialog.hotkeys.reset("Navigate back")
        assert _override_for(_read_hotkeys_json(tmp_path), "nav.back") is None


class TestRestoreAllDefaults(ExpLorerFreshAppTestCase):
    def test_cancel_leaves_everything_unchanged(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")
        dialog.hotkeys.record_chord("Navigate back", "^%9")
        before = _read_hotkeys_json(tmp_path)

        dialog.hotkeys.click_restore_all()
        dialog.hotkeys.confirm_restore_all(accept=False)

        assert _read_hotkeys_json(tmp_path) == before

    def test_confirm_resets_every_row(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")
        dialog.hotkeys.record_chord("Navigate back", "^%9")
        dialog.hotkeys.record_chord("New tab", "^%8")

        dialog.hotkeys.click_restore_all()
        dialog.hotkeys.confirm_restore_all(accept=True)

        data = _read_hotkeys_json(tmp_path)
        assert data is not None
        assert data["overrides"] == []


class TestHotkeyPersistence(ExpLorerFreshAppTestCase):
    def test_hotkeys_json_absent_until_something_changes(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        assert _read_hotkeys_json(tmp_path) is None

    def test_hotkeys_json_stores_only_the_overridden_id(self, tmp_path: Path) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")

        dialog.hotkeys.record_chord("Navigate back", "^%9")

        data = _read_hotkeys_json(tmp_path)
        assert data is not None
        assert len(data["overrides"]) == 1
        assert set(data["overrides"][0].keys()) == {"id", "chord", "order"}
        assert data["overrides"][0]["id"] == "nav.back"


class TestHotkeyRestartRoundTrip(ExpLorerRestartableTestCase):
    def test_rebind_persists_across_restart_and_still_fires(self) -> None:
        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")
        dialog.hotkeys.record_chord("New tab", "^%9")  # Ctrl+Alt+9, window-scoped
        dialog.close()

        self.restart()

        data = json.loads((self.data_dir / "hotkeys.json").read_text(encoding="utf-8"))
        override = next(o for o in data["overrides"] if o["id"] == "tabs.new")
        assert override["chord"] == "Ctrl+Alt+9"

        before = len(self.app.panes.a.tabs.titles())
        self.app.panes.a.file_tree_view.click()
        send_keys("^%9")
        wait_until(lambda: len(self.app.panes.a.tabs.titles()) == before + 1)


# ===================== Tier 2: correctness probes =====================


class TestSameScopeDuplicateRecencyWins(ExpLorerPreparedDataDirTestCase):
    """Pre-seeds `hotkeys.json` directly (via `ExpLorerPreparedDataDirTestCase`) instead of
    recording both chords through the Settings UI, to sidestep a SEPARATE, confirmed production
    bug (see this session's final report): typing a chord into the Settings page's recording
    widget while that exact chord is already live-bound to a Window-scoped action gets hijacked by
    the globally-installed `HotkeyWindowDispatcher` before the recording widget ever sees the
    keystroke -- so two Window-scoped actions can never actually be bound to the identical chord
    through the Settings UI itself, only by pre-seeding the file like this.
    """

    def prepare_data_dir(self, data_dir: Path) -> None:
        (data_dir / "hotkeys.json").write_text(
            json.dumps(
                {
                    "version": 1,
                    "nextOrder": 3,
                    "overrides": [
                        {"id": "tabs.new", "chord": "Ctrl+Alt+7", "order": 1},
                        {"id": "tabs.close", "chord": "Ctrl+Alt+7", "order": 2},  # more recent
                    ],
                }
            ),
            encoding="utf-8",
        )

    def test_most_recently_bound_action_fires_not_the_other(self) -> None:
        """Two Window-scoped Tabs actions ("New tab"/"Close tab") bound to the identical chord --
        spec §5: only the more-recently-bound one fires, confirmed functionally (tab count).
        """
        self.app.navigation.new_tab.click()
        wait_until(lambda: len(self.app.panes.a.tabs.titles()) == 2)

        self.app.panes.a.file_tree_view.click()
        send_keys("^%7")
        wait_until(lambda: len(self.app.panes.a.tabs.titles()) == 1)


class TestCrossScopeDuplicateBinding(ExpLorerFreshAppTestCase):
    def test_cross_scope_duplicate_chord_both_still_fire_independently(self, tmp_path: Path) -> None:
        """Spec §5 / `HotkeyManager::conflictingAction`'s own doc comment: cross-scope duplicates
        "never contend" -- i.e. a View-scoped and a Window-scoped action sharing one chord should
        each still independently fire correctly in their own context (View-scoped while the file
        browser view has focus; Window-scoped everywhere). "Select all" (View-scoped,
        fileOps.selectAll) and "New tab" (Window-scoped, tabs.new) are bound to the identical
        custom chord here, then pressed while the file list has focus -- the one place both
        actions' own context conditions are simultaneously satisfied.

        CONFIRMED BY HAND (see this session's final report) that this currently fails: the
        Window-scoped action always wins and the View-scoped one never fires at all, regardless of
        which was bound more recently -- `HotkeyWindowDispatcher` is a QApplication-wide event
        filter that intercepts every key event before `FileBrowserView`'s own (lower-precedence,
        per-object) event filter ever sees it. This test asserts the behavior the spec promises,
        not the current (buggy) behavior -- it is expected to fail until that's fixed.
        """
        target = tmp_path / "target"
        target.mkdir()
        (target / "a.txt").write_text("x")
        (target / "b.txt").write_text("x")
        self.app.navigation.navigate_to(str(target))

        dialog = open_settings_dialog(self.app)
        dialog.select_category("Hotkeys")
        dialog.hotkeys.record_chord("Select all", "^%5")
        dialog.hotkeys.record_chord("New tab", "^%5")
        dialog.close()

        tree_view = self.app.panes.a.file_tree_view
        tree_view.raw.set_focus()
        before_tab_count = len(self.app.panes.a.tabs.titles())

        send_keys("^%5")
        time.sleep(0.5)

        assert selected_names(tree_view) == ["a.txt", "b.txt"], (
            "View-scoped 'Select all' should have fired while the file list had focus"
        )
        assert len(self.app.panes.a.tabs.titles()) == before_tab_count, (
            "Window-scoped 'New tab' should NOT also have fired for the same keypress"
        )


# ===================== Tier 3: representative runtime-dispatch sampling =====================


class TestNavigationHotkeys(ExpLorerFreshAppTestCase):
    def test_alt_left_right_traverse_history_like_the_mouse_buttons(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.navigate_to(str(second))
        self.app.panes.a.file_tree_view.click()

        send_keys("%{LEFT}")
        wait_until(lambda: self.app.navigation.address_bar.text == str(first))

        send_keys("%{RIGHT}")
        wait_until(lambda: self.app.navigation.address_bar.text == str(second))


class TestTabsHotkeysWorkRegardlessOfFocus(ExpLorerFreshAppTestCase):
    def test_ctrl_t_and_ctrl_w_work_while_address_bar_is_focused(self) -> None:
        """Window-scoped: must fire even with a QLineEdit (which has its own idea of what Ctrl+T/
        Ctrl+W might mean) focused -- the `QEvent::ShortcutOverride`-claims-first mechanism
        `HotkeyWindowDispatcher` documents.
        """
        before = len(self.app.panes.a.tabs.titles())
        self.app.navigation.address_bar.click()

        send_keys("^t")
        wait_until(lambda: len(self.app.panes.a.tabs.titles()) == before + 1)

        send_keys("^w")
        wait_until(lambda: len(self.app.panes.a.tabs.titles()) == before)


class TestPaneFocusHotkey(ExpLorerTestCase):
    """Window-scoped, multi-pane: uses the shared-instance base class (not
    `ExpLorerFreshAppTestCase`) since it needs `configure_workspace` to boot straight into a
    two-pane layout -- harmless here since this class has exactly one test method, so there is no
    risk of one method's hotkey rebinds leaking into another's.
    """

    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL
        config.workspace.panes.b.add_tab()  # a pane with no tabs configured stays hidden

    def test_ctrl_alt_2_focuses_pane_b(self) -> None:
        wait_until(lambda: self.app.panes.b.file_browser.exists())
        self.app.panes.a.file_tree_view.click()

        send_keys("^%2")

        wait_until(lambda: self.app.panes.b.file_tree_view.raw.has_keyboard_focus())


class TestPanelToggleHotkeyRemembersLastActiveTab(ExpLorerFreshAppTestCase):
    def test_ctrl_b_toggle_restores_last_active_tab(self) -> None:
        """`CollapsiblePanelWidget::toggleExpanded()` (M5's own flagged bug-finding target): the
        left panel must re-expand to whichever tab was active when it collapsed, not always Tree.
        """
        self.app.left_panel.search  # expands the panel on the Search tab
        assert self.app.left_panel.search_button.is_checked()

        self.app.panes.a.file_tree_view.click()
        send_keys("^b")
        wait_until(lambda: not self.app.left_panel.is_expanded())

        send_keys("^b")
        wait_until(lambda: self.app.left_panel.is_expanded())
        assert self.app.left_panel.search_button.is_checked()
        assert not self.app.left_panel.tree_button.is_checked()


class TestToggleHiddenFilesHotkey(ExpLorerFreshAppTestCase):
    def test_ctrl_h_toggles_hidden_files_via_keyboard(self, tmp_path: Path) -> None:
        """Cross-check against `test_hidden_files_toggle.py`'s existing button-based coverage --
        this is a genuinely app-wide `QSettings` preference (not `--data-dir`-scoped), so restore
        it afterward regardless of the ambient starting value, same posture as that suite.
        """
        target = tmp_path / "target"
        _make_hidden_file(target)

        before = _hidden_file_visible(self.app, target)
        try:
            self.app.panes.a.file_tree_view.click()
            send_keys("^h")
            wait_until(lambda: _hidden_file_visible(self.app, target) != before or None)
            assert _hidden_file_visible(self.app, target) != before
        finally:
            if _hidden_file_visible(self.app, target) != before:
                self.app.panes.a.file_tree_view.click()
                send_keys("^h")
                wait_until(lambda: _hidden_file_visible(self.app, target) == before or None)


class TestSearchAndToolsMenuEntries(ExpLorerFreshAppTestCase):
    def test_new_folder_has_a_menu_bar_entry_and_creates_a_folder(self, tmp_path: Path) -> None:
        target = tmp_path / "target"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))

        self.app.window.child_window(title="File", control_type="MenuItem").click_input()
        self.app.window.child_window(title="New Folder", control_type="MenuItem").click_input()
        time.sleep(0.3)
        send_keys("{ENTER}")  # commits the default "New folder" name in the inline rename editor

        wait_until(lambda: (target / "New folder").exists())

    def test_ctrl_d_adds_the_browsed_folder_to_favorites(self, tmp_path: Path) -> None:
        target = tmp_path / "target"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        self.app.panes.a.file_tree_view.click()

        send_keys("^d")
        time.sleep(0.3)

        dialog = self.app.favorites.open_manage_favorites_dialog()
        try:
            wait_until(lambda: "target" in dialog.entry_names())
        finally:
            dialog.close_button.click()
