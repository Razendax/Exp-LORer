"""Coverage for the right-hand Tag Panel (TagPanelWidget.cpp, TagListViewModel.cpp,
Architecture.md §14.9, Specification.md "Tag Panel") -- applying/creating tags on the current
file/folder selection, what's shown in each of its three sections (folder/ancestor tags, search
matches, tags on the resolved selection), removing a tag from a file/folder, and tags persisting
across an app restart (compare test_tag_manager_dialog.py, which covers the separate global
Tag Manager dialog's own add/rename/delete).

Every class uses `ExpLorerFreshAppTestCase` rather than `ExpLorerTestCase`: tags are *global*
state (one SQLite DB per data_dir, not scoped to any one folder), so a shared instance would carry
every previous test method's tags into the next one, making "this tag doesn't exist yet"/"this is
the only tag" assertions unreliable.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Callable

from framework import ExpLorerApp, ExpLorerFreshAppTestCase, find_chip
from framework.details_view import select_row
from framework.wait import wait_until


class TestApplyingAndCreatingTagsViaRightPanel(ExpLorerFreshAppTestCase):
    def test_create_and_apply_tag_to_selected_file(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "notes.txt")

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("Urgent")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Urgent"])

    def test_create_and_apply_tag_to_browsed_folder_when_nothing_selected(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("ImportantFolder")
        wait_until(lambda: tags_panel.selected_item_tags() == ["ImportantFolder"])

    def test_apply_existing_tag_via_search_matches_plus_button(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()
        (first / "a.txt").write_text("a")
        (second / "b.txt").write_text("b")

        self.app.navigation.navigate_to(str(first))
        wait_until(lambda: self.app.navigation.address_bar.text == str(first))
        select_row(self.app.panes.a.file_tree_view, "a.txt")
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("Shared")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Shared"])

        self.app.navigation.navigate_to(str(second))
        wait_until(lambda: self.app.navigation.address_bar.text == str(second))
        select_row(self.app.panes.a.file_tree_view, "b.txt")

        tags_panel.search_for("Shared")
        wait_until(lambda: "Shared" in tags_panel.search_matches())
        find_chip(tags_panel.search_matches_section.raw, "Shared").click_add()

        wait_until(lambda: tags_panel.selected_item_tags() == ["Shared"])

    def test_created_tag_is_visible_in_search_from_a_different_target(self, tmp_path: Path) -> None:
        # Confirms the right panel and the (separately-covered) Tag Manager dialog share one
        # global tag repository.
        target = tmp_path / "browse"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("GloballyVisible")
        wait_until(lambda: tags_panel.selected_item_tags() == ["GloballyVisible"])

        other = tmp_path / "other"
        other.mkdir()
        self.app.navigation.navigate_to(str(other))
        wait_until(lambda: self.app.navigation.address_bar.text == str(other))

        tags_panel.search_for("GloballyVisible")
        wait_until(lambda: "GloballyVisible" in tags_panel.search_matches())

    def test_search_matches_excludes_tags_already_on_the_target(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("AlreadyApplied")
        wait_until(lambda: tags_panel.selected_item_tags() == ["AlreadyApplied"])

        tags_panel.search_for("AlreadyApplied")
        time.sleep(0.4)
        assert "AlreadyApplied" not in tags_panel.search_matches()

    def test_create_tag_affordance_hidden_for_an_exact_existing_name(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("Exact")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Exact"])

        other = tmp_path / "other"
        other.mkdir()
        self.app.navigation.navigate_to(str(other))
        wait_until(lambda: self.app.navigation.address_bar.text == str(other))

        tags_panel.search_for("Exact")
        time.sleep(0.4)
        assert not tags_panel.create_tag_button.exists()

        # a case-different query still counts as an exact (case-insensitive) match
        tags_panel.search_for("EXACT")
        time.sleep(0.4)
        assert not tags_panel.create_tag_button.exists()

    def test_tagging_with_multiple_items_selected_does_nothing(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "a.txt").write_text("a")
        (target / "b.txt").write_text("b")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        tags_panel = self.app.right_panel.tags
        select_row(self.app.panes.a.file_tree_view, "a.txt")
        select_row(self.app.panes.a.file_tree_view, "b.txt", additive=True)
        time.sleep(0.3)

        assert tags_panel.selected_item_tags() == []

        tags_panel.create_and_apply_tag("NeverAssigned")
        time.sleep(0.6)

        assert self.app.exists()
        assert tags_panel.selected_item_tags() == []

    def test_tagging_at_this_pc_root_does_not_crash(self) -> None:
        assert self.app.navigation.address_bar.text == "This PC"

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("AtThisPC")
        time.sleep(0.6)

        assert self.app.exists()
        assert self.app.navigation.address_bar.text == "This PC"

    def test_non_ascii_tag_name_applies_to_file(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "notes.txt")

        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("日本語タグ_😀")
        wait_until(lambda: tags_panel.selected_item_tags() == ["日本語タグ_😀"])


class TestTagDisplayAcrossFoldersAndSelection(ExpLorerFreshAppTestCase):
    def test_tag_on_a_folder_is_shown_while_browsing_into_it(self, tmp_path: Path) -> None:
        outer = tmp_path / "outer"
        outer.mkdir()

        self.app.navigation.navigate_to(str(outer))
        wait_until(lambda: self.app.navigation.address_bar.text == str(outer))
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("OnOuter")
        wait_until(lambda: tags_panel.selected_item_tags() == ["OnOuter"])

        assert "OnOuter" in tags_panel.folder_tags()

    def test_tag_on_an_ancestor_is_shown_while_browsing_a_descendant(self, tmp_path: Path) -> None:
        outer = tmp_path / "outer"
        inner = outer / "inner"
        inner.mkdir(parents=True)

        self.app.navigation.navigate_to(str(outer))
        wait_until(lambda: self.app.navigation.address_bar.text == str(outer))
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("OnAncestor")
        wait_until(lambda: tags_panel.selected_item_tags() == ["OnAncestor"])

        self.app.navigation.navigate_to(str(inner))
        wait_until(lambda: self.app.navigation.address_bar.text == str(inner))
        wait_until(lambda: "OnAncestor" in tags_panel.folder_tags())

    def test_selecting_a_tagged_file_shows_its_tags_under_selection(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "tagged.txt").write_text("x")
        (target / "untagged.txt").write_text("y")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "tagged.txt")
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("OnFile")
        wait_until(lambda: tags_panel.selected_item_tags() == ["OnFile"])

        select_row(self.app.panes.a.file_tree_view, "untagged.txt")
        wait_until(lambda: tags_panel.selected_item_tags() == [])

        select_row(self.app.panes.a.file_tree_view, "tagged.txt")
        wait_until(lambda: tags_panel.selected_item_tags() == ["OnFile"])

    def test_switching_selection_between_two_tagged_files_updates_the_section(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "one.txt").write_text("1")
        (target / "two.txt").write_text("2")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        tags_panel = self.app.right_panel.tags

        select_row(self.app.panes.a.file_tree_view, "one.txt")
        tags_panel.create_and_apply_tag("TagOne")
        wait_until(lambda: tags_panel.selected_item_tags() == ["TagOne"])

        select_row(self.app.panes.a.file_tree_view, "two.txt")
        tags_panel.create_and_apply_tag("TagTwo")
        wait_until(lambda: tags_panel.selected_item_tags() == ["TagTwo"])

        select_row(self.app.panes.a.file_tree_view, "one.txt")
        wait_until(lambda: tags_panel.selected_item_tags() == ["TagOne"])

    def test_non_ascii_folder_and_file_names_display_their_tags(self, tmp_path: Path) -> None:
        target = tmp_path / "Папка_日本語_😀"
        target.mkdir()
        (target / "файл.txt").write_text("hi", encoding="utf-8")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "файл.txt")
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("NonAsciiTarget")
        wait_until(lambda: tags_panel.selected_item_tags() == ["NonAsciiTarget"])


class TestDeletingTagFromFileOrFolder(ExpLorerFreshAppTestCase):
    def test_remove_tag_from_a_file_via_the_x_button(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "notes.txt")
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("Removable")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Removable"])

        find_chip(tags_panel.selected_item_tags_section.raw, "Removable").click_remove()
        wait_until(lambda: tags_panel.selected_item_tags() == [])

    def test_remove_tag_from_a_folder_via_the_x_button(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("OnFolder")
        wait_until(lambda: tags_panel.selected_item_tags() == ["OnFolder"])

        find_chip(tags_panel.selected_item_tags_section.raw, "OnFolder").click_remove()
        wait_until(lambda: tags_panel.selected_item_tags() == [])
        wait_until(lambda: "OnFolder" not in tags_panel.folder_tags())

    def test_removing_a_tag_from_one_file_leaves_it_on_another(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "one.txt").write_text("1")
        (target / "two.txt").write_text("2")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        tags_panel = self.app.right_panel.tags

        select_row(self.app.panes.a.file_tree_view, "one.txt")
        tags_panel.create_and_apply_tag("Shared")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Shared"])

        select_row(self.app.panes.a.file_tree_view, "two.txt")
        tags_panel.search_for("Shared")
        wait_until(lambda: "Shared" in tags_panel.search_matches())
        find_chip(tags_panel.search_matches_section.raw, "Shared").click_add()
        wait_until(lambda: tags_panel.selected_item_tags() == ["Shared"])

        find_chip(tags_panel.selected_item_tags_section.raw, "Shared").click_remove()
        wait_until(lambda: tags_panel.selected_item_tags() == [])

        select_row(self.app.panes.a.file_tree_view, "one.txt")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Shared"])

    def test_untagging_a_file_does_not_delete_the_tag_globally(self, tmp_path: Path) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")

        self.app.navigation.navigate_to(str(target))
        wait_until(lambda: self.app.navigation.address_bar.text == str(target))
        select_row(self.app.panes.a.file_tree_view, "notes.txt")
        tags_panel = self.app.right_panel.tags
        tags_panel.create_and_apply_tag("StillExists")
        wait_until(lambda: tags_panel.selected_item_tags() == ["StillExists"])

        find_chip(tags_panel.selected_item_tags_section.raw, "StillExists").click_remove()
        wait_until(lambda: tags_panel.selected_item_tags() == [])

        tags_panel.search_for("StillExists")
        wait_until(lambda: "StillExists" in tags_panel.search_matches())


class TestTagPersistenceAcrossRestart:
    """Uses the `start_app` factory fixture (conftest.py) instead of `ExpLorerFreshAppTestCase`
    -- these tests need two separate Exp-LORer instances pointed at the same data_dir (one to
    apply a tag and close, one to reopen and check it's still there), which needs more control
    over the instance lifecycle than a single shared/per-method `self.app` gives.
    """

    def test_tag_on_a_file_persists_after_restart(
        self, start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
    ) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        (target / "notes.txt").write_text("hi")
        data_dir = tmp_path / "data"

        app = start_app(data_dir)
        app.navigation.navigate_to(str(target))
        wait_until(lambda: app.navigation.address_bar.text == str(target))
        select_row(app.panes.a.file_tree_view, "notes.txt")
        tags_panel = app.right_panel.tags
        tags_panel.create_and_apply_tag("Persisted")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Persisted"])
        app.close()

        ExpLorerApp.kill_stray_instances()
        app2 = start_app(data_dir)
        app2.navigation.navigate_to(str(target))
        wait_until(lambda: app2.navigation.address_bar.text == str(target))
        tags_panel2 = app2.right_panel.tags
        select_row(app2.panes.a.file_tree_view, "notes.txt")
        wait_until(lambda: tags_panel2.selected_item_tags() == ["Persisted"])

    def test_tag_on_a_folder_persists_after_restart(
        self, start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
    ) -> None:
        outer = tmp_path / "outer"
        inner = outer / "inner"
        inner.mkdir(parents=True)
        data_dir = tmp_path / "data"

        app = start_app(data_dir)
        app.navigation.navigate_to(str(outer))
        wait_until(lambda: app.navigation.address_bar.text == str(outer))
        tags_panel = app.right_panel.tags
        tags_panel.create_and_apply_tag("PersistedFolder")
        wait_until(lambda: tags_panel.selected_item_tags() == ["PersistedFolder"])
        app.close()

        ExpLorerApp.kill_stray_instances()
        app2 = start_app(data_dir)
        app2.navigation.navigate_to(str(inner))
        wait_until(lambda: app2.navigation.address_bar.text == str(inner))
        tags_panel2 = app2.right_panel.tags
        wait_until(lambda: "PersistedFolder" in tags_panel2.folder_tags())

    def test_tag_list_itself_persists_even_when_unused_after_restart(
        self, start_app: Callable[[Path], ExpLorerApp], tmp_path: Path
    ) -> None:
        target = tmp_path / "browse"
        target.mkdir()
        data_dir = tmp_path / "data"

        app = start_app(data_dir)
        app.navigation.navigate_to(str(target))
        wait_until(lambda: app.navigation.address_bar.text == str(target))
        tags_panel = app.right_panel.tags
        tags_panel.create_and_apply_tag("GlobalAfterRestart")
        wait_until(lambda: tags_panel.selected_item_tags() == ["GlobalAfterRestart"])
        app.close()

        ExpLorerApp.kill_stray_instances()
        app2 = start_app(data_dir)
        other = tmp_path / "other"
        other.mkdir()
        app2.navigation.navigate_to(str(other))
        wait_until(lambda: app2.navigation.address_bar.text == str(other))

        tags_panel2 = app2.right_panel.tags
        tags_panel2.search_for("GlobalAfterRestart")
        wait_until(lambda: "GlobalAfterRestart" in tags_panel2.search_matches())
