"""Coverage for the Favorites menu's "Add to Favorites" action (`MainWindow::onAddToFavoritesTriggered`,
Architecture.md §14.35) -- adding the current file/folder selection (or the browsed folder itself
when nothing is selected) as a favorite, and when the action is enabled at all. Compare
test_manage_favorites_dialog.py, which covers the separate "Manage Favorites..." dialog's own
add-category/rename/reorder/remove operations.

Every class uses `ExpLorerFreshAppTestCase` rather than `ExpLorerTestCase`: favorites are *global*
state (one favorites.json per data_dir, not scoped to any one folder -- CompositionRoot.cpp), so a
shared instance would carry every previous test method's favorites into the next one, making
"this is the only favorite" assertions unreliable.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.details_view import select_row
from framework.wait import wait_until


class TestAddToFavoritesViaMenu(ExpLorerFreshAppTestCase):
    def test_add_to_favorites_disabled_at_this_pc_root(self) -> None:
        assert self.app.navigation.address_bar.text == "This PC"
        assert not self.app.favorites.add_to_favorites_enabled()

    def test_add_to_favorites_enabled_after_navigating_into_a_folder(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        assert self.app.favorites.add_to_favorites_enabled()

    def test_add_to_favorites_with_no_selection_adds_the_browsed_folder(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        self.app.favorites.add_to_favorites()

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["browse"])

    def test_add_to_favorites_with_a_selected_file_adds_that_file(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "notes.txt")
        self.app.favorites.add_to_favorites()

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["notes.txt"])

    def test_add_to_favorites_with_multiple_selected_entries_adds_each_as_its_own_favorite(
        self, tmp_path: Path
    ) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "a.txt").write_text("a")
        (target / "b.txt").write_text("b")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "a.txt")
        select_row(self.app.panes.a.file_tree_view, "b.txt", additive=True)
        self.app.favorites.add_to_favorites()

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: set(dlg.entry_names()) == {"a.txt", "b.txt"})

    def test_add_to_favorites_twice_is_idempotent(self, tmp_path: Path) -> None:
        # Favorites::addItem's findExistingItemId dedups by path -- re-adding an already-favorited
        # path returns the existing entry's id rather than appending a second one.
        target = tmp_path / "browse"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        self.app.favorites.add_to_favorites()
        self.app.favorites.add_to_favorites()

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["browse"])

    def test_non_ascii_folder_name_round_trips_as_a_favorite(self, tmp_path: Path) -> None:
        target = tmp_path / "Папка_日本語_😀"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        self.app.favorites.add_to_favorites()

        dlg = self.app.favorites.open_manage_favorites_dialog()
        wait_until(lambda: dlg.entry_names() == ["Папка_日本語_😀"])

    def test_manage_favorites_dialog_opens_empty_with_no_favorites_added(self) -> None:
        dlg = self.app.favorites.open_manage_favorites_dialog()
        assert dlg.entry_names() == []
