"""Coverage for the standalone Tag Manager dialog (Edit > "Tag Edit...", TagManagerDialog.cpp,
Architecture.md §14.17) -- creating, renaming, and deleting tags globally, independent of any
file/folder selection (compare test_tag_panel.py, which covers applying/removing tags on a
selection via the right-hand Tag Panel).

Uses `ExpLorerFreshAppTestCase` (a fresh Exp-LORer instance per test method) rather than
`ExpLorerTestCase` -- the dialog's tag list is *global* state, not scoped to any per-test
tmp_path, so a shared instance would carry every previous test method's tags into the next one,
making the exact `chip_names()` sets these tests assert on unreliable.
"""

from __future__ import annotations

import time

from framework import ExpLorerFreshAppTestCase, connect_by_handle, find_control, open_tag_manager_dialog
from framework.tag_manager_dialog import TagManagerDialog
from framework.wait import wait_until


class TestTagManagerDialogAddRenameDelete(ExpLorerFreshAppTestCase):
    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_add_tag_shows_a_new_chip(self) -> None:
        dlg = self._open()
        dlg.add_tag("Urgent")
        wait_until(lambda: dlg.chip_names() == ["Urgent"])

    def test_added_tag_name_is_trimmed(self) -> None:
        dlg = self._open()
        dlg.add_tag("  Spacey  ")
        wait_until(lambda: dlg.chip_names() == ["Spacey"])

    def test_add_with_empty_name_does_nothing(self) -> None:
        dlg = self._open()
        input_dlg = dlg.open_add_dialog()
        # leave the name field blank, accept anyway
        find_control(input_dlg, "Button", "OK").click_input()
        time.sleep(0.5)
        assert dlg.chip_names() == []

    def test_add_cancelled_does_nothing(self) -> None:
        dlg = self._open()
        input_dlg = dlg.open_add_dialog()
        find_control(input_dlg, "Edit").set_edit_text("NeverCreated")
        find_control(input_dlg, "Button", "Cancel").click_input()
        time.sleep(0.5)
        assert dlg.chip_names() == []

    def test_duplicate_tag_name_is_rejected_with_a_warning(self) -> None:
        dlg = self._open()
        dlg.add_tag("Urgent")
        wait_until(lambda: dlg.chip_names() == ["Urgent"])

        dlg.add_tag_expect_failure("Urgent")
        time.sleep(0.3)
        assert dlg.chip_names() == ["Urgent"]

    def test_differently_cased_tag_name_is_not_treated_as_a_duplicate(self) -> None:
        # Tags.name's UNIQUE constraint (SQLiteTagRepository.cpp's migration) has no COLLATE
        # NOCASE, so uniqueness is case-sensitive -- "Urgent" and "URGENT" coexist as two
        # distinct tags rather than the second being rejected.
        dlg = self._open()
        dlg.add_tag("Urgent")
        wait_until(lambda: dlg.chip_names() == ["Urgent"])

        dlg.add_tag("URGENT")
        wait_until(lambda: set(dlg.chip_names()) == {"Urgent", "URGENT"})

    def test_rename_tag_updates_its_chip(self) -> None:
        dlg = self._open()
        dlg.add_tag("Old")
        wait_until(lambda: dlg.chip_names() == ["Old"])

        dlg.rename_tag("Old", "New")
        wait_until(lambda: dlg.chip_names() == ["New"])

    def test_rename_tag_name_is_trimmed(self) -> None:
        dlg = self._open()
        dlg.add_tag("Old")
        wait_until(lambda: dlg.chip_names() == ["Old"])

        dlg.rename_tag("Old", "  New  ")
        wait_until(lambda: dlg.chip_names() == ["New"])

    def test_rename_to_an_existing_name_is_rejected_with_a_warning(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        dlg.add_tag("Beta")
        wait_until(lambda: set(dlg.chip_names()) == {"Alpha", "Beta"})

        dlg.rename_tag_expect_failure("Alpha", "Beta")
        time.sleep(0.3)
        assert set(dlg.chip_names()) == {"Alpha", "Beta"}

    def test_rename_to_the_same_name_is_a_silent_noop(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        wait_until(lambda: dlg.chip_names() == ["Alpha"])

        input_dlg = dlg.open_rename_dialog("Alpha")
        # don't change the pre-filled text at all
        find_control(input_dlg, "Button", "OK").click_input()
        time.sleep(0.5)
        assert dlg.chip_names() == ["Alpha"]

    def test_rename_cancelled_keeps_the_original_name(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        wait_until(lambda: dlg.chip_names() == ["Alpha"])

        input_dlg = dlg.open_rename_dialog("Alpha")
        find_control(input_dlg, "Edit").set_edit_text("NeverApplied")
        find_control(input_dlg, "Button", "Cancel").click_input()
        time.sleep(0.5)
        assert dlg.chip_names() == ["Alpha"]

    def test_delete_tag_removes_its_chip_after_confirmation(self) -> None:
        dlg = self._open()
        dlg.add_tag("Temporary")
        wait_until(lambda: dlg.chip_names() == ["Temporary"])

        dlg.delete_tag("Temporary", confirm=True)
        wait_until(lambda: dlg.chip_names() == [])

    def test_delete_cancelled_keeps_the_tag(self) -> None:
        dlg = self._open()
        dlg.add_tag("Keepme")
        wait_until(lambda: dlg.chip_names() == ["Keepme"])

        dlg.delete_tag("Keepme", confirm=False)
        time.sleep(0.5)
        assert dlg.chip_names() == ["Keepme"]

    def test_non_ascii_tag_name_round_trips(self) -> None:
        # Non-ASCII names are a hard requirement (Specification.md line 34) -- exercise it for
        # tags too, not just file/folder paths.
        dlg = self._open()
        dlg.add_tag("日本語タグ_😀")
        wait_until(lambda: dlg.chip_names() == ["日本語タグ_😀"])

    def test_rename_and_delete_buttons_disabled_until_a_chip_is_selected(self) -> None:
        dlg = self._open()
        assert not dlg.rename_button.is_enabled()
        assert not dlg.delete_button.is_enabled()

        dlg.add_tag("Alpha")
        wait_until(lambda: dlg.chip_names() == ["Alpha"])
        # adding a tag doesn't itself select its chip
        assert not dlg.rename_button.is_enabled()
        assert not dlg.delete_button.is_enabled()

        dlg.click_chip("Alpha")
        assert dlg.rename_button.is_enabled()
        assert dlg.delete_button.is_enabled()

    def test_search_filters_the_chip_list(self) -> None:
        dlg = self._open()
        dlg.add_tag("Apple")
        dlg.add_tag("Banana")
        dlg.add_tag("Apricot")
        wait_until(lambda: set(dlg.chip_names()) == {"Apple", "Banana", "Apricot"})

        dlg.search_edit.click()
        dlg.search_edit.set_text("Ap")
        wait_until(lambda: set(dlg.chip_names()) == {"Apple", "Apricot"})

    def test_close_button_closes_the_dialog(self) -> None:
        dlg = self._open()
        assert dlg.exists()
        dlg.close()
        wait_until(lambda: not dlg.exists())
