"""Coverage for the standalone Tag Manager dialog (Edit > "Tag Edit...", TagManagerDialog.cpp,
Architecture.md §14.17) -- creating, renaming, and deleting tags globally, independent of any
file/folder selection (compare test_tag_panel.py, which covers applying/removing tags on a
selection via the right-hand Tag Panel). Also covers the dialog's later additions: per-chip file
counts and the per-tag files/folders results view (§14.89/§14.90), alphabetical/count sorting
(§14.92), the List/Tree view-mode switch over nested tags (§14.88/§14.91), and the
rename-into-an-existing-name merge offer (§14.89).

Uses `ExpLorerFreshAppTestCase` (a fresh Exp-LORer instance per test method) rather than
`ExpLorerTestCase` -- the dialog's tag list is *global* state, not scoped to any per-test
tmp_path, so a shared instance would carry every previous test method's tags into the next one,
making the exact `chip_names()` sets these tests assert on unreliable.
"""

from __future__ import annotations

import time
from pathlib import Path

from framework import ExpLorerFreshAppTestCase, find_chip, find_control, open_tag_manager_dialog
from framework.details_view import select_row
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


class TestTagManagerDialogMerge(ExpLorerFreshAppTestCase):
    """§14.89: renaming a tag into a name that already belongs to a *different* tag no longer gets
    flatly rejected -- it offers to merge the two tags (fold the renamed one's associations into
    the existing one, then remove it) instead.
    """

    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_renaming_into_an_existing_name_prompts_a_merge_offer_and_accepting_merges(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        dlg.add_tag("Beta")
        wait_until(lambda: set(dlg.chip_names()) == {"Alpha", "Beta"})

        dlg.rename_tag_merge("Alpha", "Beta", accept=True)
        wait_until(lambda: dlg.chip_names() == ["Beta"])

    def test_declining_a_merge_offer_leaves_both_tags_untouched(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        dlg.add_tag("Beta")
        wait_until(lambda: set(dlg.chip_names()) == {"Alpha", "Beta"})

        dlg.rename_tag_merge("Alpha", "Beta", accept=False)
        time.sleep(0.3)
        assert set(dlg.chip_names()) == {"Alpha", "Beta"}

    def test_merge_offer_collision_check_is_case_insensitive(self) -> None:
        dlg = self._open()
        dlg.add_tag("Alpha")
        dlg.add_tag("Beta")
        wait_until(lambda: set(dlg.chip_names()) == {"Alpha", "Beta"})

        # Typing "BETA" still collides with "Beta" -- and the merge keeps the existing tag's own
        # name/casing, not the typed one.
        dlg.rename_tag_merge("Alpha", "BETA", accept=True)
        wait_until(lambda: dlg.chip_names() == ["Beta"])


class TestTagManagerDialogCounts(ExpLorerFreshAppTestCase):
    """§14.89/§14.92: the "#" toggle shows each chip's tagged file/folder count, computed from the
    same global tag repository the right-hand Tag Panel writes to.
    """

    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_count_label_is_hidden_until_the_toggle_is_turned_on(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        (folder / "a.txt").write_text("a")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        select_row(self.app.panes.a.file_tree_view, "a.txt")
        self.app.right_panel.tags.create_and_apply_tag("Solo")
        wait_until(lambda: self.app.right_panel.tags.selected_item_tags() == ["Solo"])

        dlg = self._open()
        wait_until(lambda: dlg.chip_names() == ["Solo"])
        assert dlg.chip_count_text("Solo") is None

        dlg.show_counts_button.click()
        wait_until(lambda: dlg.chip_count_text("Solo") == "1")

        dlg.show_counts_button.click()
        wait_until(lambda: dlg.chip_count_text("Solo") is None)

    def test_count_reflects_the_number_of_tagged_files(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        (folder / "a.txt").write_text("a")
        (folder / "b.txt").write_text("b")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        tags_panel = self.app.right_panel.tags

        select_row(self.app.panes.a.file_tree_view, "a.txt")
        tags_panel.create_and_apply_tag("Reviewed")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Reviewed"])

        select_row(self.app.panes.a.file_tree_view, "b.txt")
        tags_panel.search_for("Reviewed")
        wait_until(lambda: "Reviewed" in tags_panel.search_matches())
        find_chip(tags_panel.search_matches_section.raw, "Reviewed").click_add()
        wait_until(lambda: tags_panel.selected_item_tags() == ["Reviewed"])

        dlg = self._open()
        wait_until(lambda: dlg.chip_names() == ["Reviewed"])
        dlg.show_counts_button.click()
        wait_until(lambda: dlg.chip_count_text("Reviewed") == "2")


class TestTagManagerDialogSorting(ExpLorerFreshAppTestCase):
    """§14.92: the sort-key (Alphabetical/Count) and sort-order (Ascending/Descending) combos
    reorder the chip list.
    """

    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_alphabetical_ascending_is_the_default_order(self) -> None:
        dlg = self._open()
        dlg.add_tag("Banana")
        dlg.add_tag("Apple")
        dlg.add_tag("Cherry")
        wait_until(lambda: set(dlg.chip_names()) == {"Apple", "Banana", "Cherry"})
        assert dlg.chip_names() == ["Apple", "Banana", "Cherry"]

    def test_switching_to_descending_reverses_the_alphabetical_order(self) -> None:
        dlg = self._open()
        dlg.add_tag("Banana")
        dlg.add_tag("Apple")
        dlg.add_tag("Cherry")
        wait_until(lambda: set(dlg.chip_names()) == {"Apple", "Banana", "Cherry"})

        dlg.select_sort_order("Descending")
        wait_until(lambda: dlg.chip_names() == ["Cherry", "Banana", "Apple"])

    def test_sort_by_count_orders_chips_by_their_tagged_file_count(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        for name in ("one.txt", "two.txt", "three.txt"):
            (folder / name).write_text("x")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        tags_panel = self.app.right_panel.tags

        # "Few" ends up on 1 file, "Many" on all 3 (one.txt carries both).
        select_row(self.app.panes.a.file_tree_view, "one.txt")
        tags_panel.create_and_apply_tag("Many")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Many"])
        tags_panel.create_and_apply_tag("Few")
        wait_until(lambda: set(tags_panel.selected_item_tags()) == {"Few", "Many"})

        for name in ("two.txt", "three.txt"):
            select_row(self.app.panes.a.file_tree_view, name)
            tags_panel.search_for("Many")
            wait_until(lambda: "Many" in tags_panel.search_matches())
            find_chip(tags_panel.search_matches_section.raw, "Many").click_add()
            wait_until(lambda: tags_panel.selected_item_tags() == ["Many"])

        dlg = self._open()
        wait_until(lambda: set(dlg.chip_names()) == {"Few", "Many"})

        dlg.select_sort_key("Count")
        wait_until(lambda: dlg.chip_names() == ["Few", "Many"])  # ascending: 1 then 3

        dlg.select_sort_order("Descending")
        wait_until(lambda: dlg.chip_names() == ["Many", "Few"])

    def test_sort_by_count_breaks_ties_alphabetically_regardless_of_order(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        (folder / "x.txt").write_text("x")
        (folder / "y.txt").write_text("y")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        tags_panel = self.app.right_panel.tags

        select_row(self.app.panes.a.file_tree_view, "x.txt")
        tags_panel.create_and_apply_tag("Zebra")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Zebra"])

        select_row(self.app.panes.a.file_tree_view, "y.txt")
        tags_panel.create_and_apply_tag("Yak")
        wait_until(lambda: tags_panel.selected_item_tags() == ["Yak"])

        dlg = self._open()
        wait_until(lambda: set(dlg.chip_names()) == {"Yak", "Zebra"})

        # Both tags have exactly 1 file -- a tie, which TagManagerDialog::rebuildTagChips's sort
        # comparator always breaks via ascending nameLess(a, b), ignoring the order combo.
        dlg.select_sort_key("Count")
        wait_until(lambda: dlg.chip_names() == ["Yak", "Zebra"])

        dlg.select_sort_order("Descending")
        time.sleep(0.3)
        assert dlg.chip_names() == ["Yak", "Zebra"]


class TestTagManagerDialogViewModes(ExpLorerFreshAppTestCase):
    """§14.88/§14.91: the List/Tree view-mode combo switches between the flat chip flow and a
    nested tree grouping tags by their "/"-separated name segments.
    """

    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_tree_view_groups_nested_tags_under_their_parent_segment(self) -> None:
        dlg = self._open()
        dlg.add_tag("Project/Design")
        wait_until(lambda: dlg.chip_names() == ["Project/Design"])
        dlg.add_tag("Project/Code")
        wait_until(lambda: set(dlg.chip_names()) == {"Project/Design", "Project/Code"})

        dlg.select_view_mode("Tree")
        wait_until(lambda: set(dlg.tree_item_texts()) == {"Project", "Design", "Code"})

    def test_switching_back_to_list_view_shows_the_flat_chip_names(self) -> None:
        dlg = self._open()
        dlg.add_tag("Project/Design")
        wait_until(lambda: dlg.chip_names() == ["Project/Design"])

        dlg.select_view_mode("Tree")
        wait_until(lambda: "Design" in dlg.tree_item_texts())

        dlg.select_view_mode("List")
        wait_until(lambda: dlg.chip_names() == ["Project/Design"])

    def test_selecting_a_leaf_tree_item_enables_rename_and_delete(self) -> None:
        dlg = self._open()
        dlg.add_tag("Solo")
        wait_until(lambda: dlg.chip_names() == ["Solo"])

        dlg.select_view_mode("Tree")
        wait_until(lambda: dlg.tree_item_texts() == ["Solo"])
        assert not dlg.rename_button.is_enabled()
        assert not dlg.delete_button.is_enabled()

        dlg.select_tree_item("Solo")
        assert dlg.rename_button.is_enabled()
        assert dlg.delete_button.is_enabled()

    def test_selecting_a_virtual_grouping_node_does_not_enable_rename_or_delete(self) -> None:
        dlg = self._open()
        dlg.add_tag("Project/Design")
        wait_until(lambda: dlg.chip_names() == ["Project/Design"])

        dlg.select_view_mode("Tree")
        wait_until(lambda: set(dlg.tree_item_texts()) == {"Project", "Design"})

        # "Project" is never an actual tag here -- only "Project/Design" is -- so it's a pure
        # path-grouping node with no backing Tag::Id (TagManagerDialog::rebuildTagTree).
        dlg.select_tree_item("Project")
        assert not dlg.rename_button.is_enabled()
        assert not dlg.delete_button.is_enabled()


class TestTagManagerDialogResultsView(ExpLorerFreshAppTestCase):
    """§14.89/§14.90: selecting a chip (or tree leaf) populates the results view below with every
    file/folder tagged with it.
    """

    def _open(self) -> TagManagerDialog:
        return open_tag_manager_dialog(self.app)

    def test_selecting_a_chip_shows_the_files_tagged_with_it(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        (folder / "report.txt").write_text("x")
        (folder / "other.txt").write_text("y")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        select_row(self.app.panes.a.file_tree_view, "report.txt")
        self.app.right_panel.tags.create_and_apply_tag("Reviewed")
        wait_until(lambda: self.app.right_panel.tags.selected_item_tags() == ["Reviewed"])

        dlg = self._open()
        wait_until(lambda: dlg.chip_names() == ["Reviewed"])
        assert dlg.result_names() == []

        dlg.click_chip("Reviewed")
        wait_until(lambda: dlg.result_names() == ["report.txt"])

    def test_switching_chip_selection_updates_the_results_view(self, tmp_path: Path) -> None:
        folder = tmp_path / "docs"
        folder.mkdir()
        (folder / "a.txt").write_text("a")
        (folder / "b.txt").write_text("b")

        self.app.navigation.navigate_to(str(folder))
        wait_until(lambda: self.app.navigation.address_bar.text == str(folder))
        tags_panel = self.app.right_panel.tags

        select_row(self.app.panes.a.file_tree_view, "a.txt")
        tags_panel.create_and_apply_tag("TagA")
        wait_until(lambda: tags_panel.selected_item_tags() == ["TagA"])

        select_row(self.app.panes.a.file_tree_view, "b.txt")
        tags_panel.create_and_apply_tag("TagB")
        wait_until(lambda: tags_panel.selected_item_tags() == ["TagB"])

        dlg = self._open()
        wait_until(lambda: set(dlg.chip_names()) == {"TagA", "TagB"})

        dlg.click_chip("TagA")
        wait_until(lambda: dlg.result_names() == ["a.txt"])

        dlg.click_chip("TagB")
        wait_until(lambda: dlg.result_names() == ["b.txt"])
