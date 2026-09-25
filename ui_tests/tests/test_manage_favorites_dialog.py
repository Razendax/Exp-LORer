"""Coverage for the standalone Manage Favorites dialog (Favorites > "Manage Favorites...",
ManageFavoritesDialog.cpp, Architecture.md §14.35) -- adding categories, renaming them, reordering
and removing entries. Compare test_favorites_menu.py, which covers the *other* way to add a
favorite (the Favorites menu's "Add to Favorites" action) and uses this dialog only to verify what
landed in favorites.json.

Every class uses `ExpLorerFreshAppTestCase` rather than `ExpLorerTestCase`: favorites are *global*
state (one favorites.json per data_dir), so a shared instance would carry every previous test
method's favorites into the next one, making these dialogs' exact `entry_names()` lists unreliable.

Adding a favorite via the dialog's own "Add File..."/"Add Folder..." buttons isn't covered here --
both open a native Windows file/folder picker (`QFileDialog::getOpenFileName`/
`getExistingDirectory`), which this repo has no established UI-Automation pattern for driving (see
`favorites_menu.py`'s module docstring for the same gap on the menu side). Favorites for these
tests are seeded via `app.favorites.add_to_favorites()` instead.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestManageFavoritesDialogCategories(ExpLorerFreshAppTestCase):
    def _open(self):
        return self.app.favorites.open_manage_favorites_dialog()

    def test_add_category_shows_a_new_row(self) -> None:
        dlg = self._open()
        dlg.add_category("Work")
        wait_until(lambda: dlg.entry_names() == ["Work"])

    def test_added_category_name_is_trimmed(self) -> None:
        dlg = self._open()
        dlg.add_category("  Spacey  ")
        wait_until(lambda: dlg.entry_names() == ["Spacey"])

    def test_rename_category_updates_its_row(self) -> None:
        dlg = self._open()
        dlg.add_category("Old")
        wait_until(lambda: dlg.entry_names() == ["Old"])

        dlg.rename_category("Old", "New")
        wait_until(lambda: dlg.entry_names() == ["New"])

    def test_rename_and_move_and_remove_buttons_disabled_until_a_row_is_selected(self) -> None:
        dlg = self._open()
        assert not dlg.rename_button.is_enabled()
        assert not dlg.remove_button.is_enabled()

        dlg.add_category("Alpha")
        wait_until(lambda: dlg.entry_names() == ["Alpha"])
        # adding a category doesn't itself select its row
        assert not dlg.rename_button.is_enabled()
        assert not dlg.remove_button.is_enabled()

        dlg.select_entry("Alpha")
        assert dlg.rename_button.is_enabled()
        assert dlg.remove_button.is_enabled()
        # a category isn't itself movable into another category
        assert not dlg.move_to_category_button.is_enabled()

    def test_remove_category_removes_its_row(self) -> None:
        dlg = self._open()
        dlg.add_category("Temporary")
        wait_until(lambda: dlg.entry_names() == ["Temporary"])

        dlg.remove_entry("Temporary")
        wait_until(lambda: dlg.entry_names() == [])

    def test_non_ascii_category_name_round_trips(self) -> None:
        dlg = self._open()
        dlg.add_category("日本語カテゴリ_😀")
        wait_until(lambda: dlg.entry_names() == ["日本語カテゴリ_😀"])

    def test_close_button_closes_the_dialog(self) -> None:
        dlg = self._open()
        assert dlg.exists()
        dlg.close()
        wait_until(lambda: not dlg.exists())


class TestManageFavoritesDialogItemsAddedViaMenu(ExpLorerFreshAppTestCase):
    """Seeds favorites via `app.favorites.add_to_favorites()` (the Favorites menu), then exercises
    this dialog's reordering/removal on them -- see module docstring for why the dialog's own
    Add File/Add Folder buttons aren't used to seed these.
    """

    def _add_favorite_folder(self, tmp_path: Path, name: str) -> None:
        target = tmp_path / name
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        self.app.favorites.add_to_favorites()

    def test_move_up_reorders_entries(self, tmp_path: Path) -> None:
        self._add_favorite_folder(tmp_path, "first")
        self._add_favorite_folder(tmp_path, "second")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["first", "second"])

        dlg.move_entry_up("second")
        wait_until(lambda: dlg.entry_names() == ["second", "first"])

    def test_move_down_reorders_entries(self, tmp_path: Path) -> None:
        self._add_favorite_folder(tmp_path, "first")
        self._add_favorite_folder(tmp_path, "second")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["first", "second"])

        dlg.move_entry_down("first")
        wait_until(lambda: dlg.entry_names() == ["second", "first"])

    def test_move_up_disabled_for_the_first_entry(self, tmp_path: Path) -> None:
        self._add_favorite_folder(tmp_path, "first")
        self._add_favorite_folder(tmp_path, "second")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["first", "second"])

        dlg.select_entry("first")
        assert not dlg.move_up_button.is_enabled()
        assert dlg.move_down_button.is_enabled()

    def test_move_down_disabled_for_the_last_entry(self, tmp_path: Path) -> None:
        self._add_favorite_folder(tmp_path, "first")
        self._add_favorite_folder(tmp_path, "second")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["first", "second"])

        dlg.select_entry("second")
        assert not dlg.move_down_button.is_enabled()
        assert dlg.move_up_button.is_enabled()

    def test_remove_item_removes_only_that_entry(self, tmp_path: Path) -> None:
        self._add_favorite_folder(tmp_path, "keep")
        self._add_favorite_folder(tmp_path, "drop")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: set(dlg.entry_names()) == {"keep", "drop"})

        dlg.remove_entry("drop")
        wait_until(lambda: dlg.entry_names() == ["keep"])

    def test_item_favorite_has_rename_and_move_to_category_disabled_or_enabled_appropriately(
        self, tmp_path: Path
    ) -> None:
        self._add_favorite_folder(tmp_path, "browse")

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["browse"])

        dlg.select_entry("browse")
        # a plain item can't be renamed from here (only categories can), but it can be filed
        # into a category
        assert not dlg.rename_button.is_enabled()
        assert dlg.move_to_category_button.is_enabled()
        assert dlg.remove_button.is_enabled()
