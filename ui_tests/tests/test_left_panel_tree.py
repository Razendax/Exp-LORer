"""Coverage for the left side panel and its folder "Tree" tab (SidePanelWidget/FolderTreeView/
FolderTreeModel, Architecture.md §14.41): expanding/collapsing the panel, the tree following the
active tab's folder (reveal + select, including across tab switches), folders-only content,
click-to-navigate, and inline F2 rename.

Every class uses `ExpLorerFreshAppTestCase`: the panel's expanded state and the tree's
expanded/loaded nodes are window-wide state a shared instance would carry from one test method
into the next.

`tmp_path` lives under the hidden AppData folder, so every reveal here also exercises the tree
showing hidden ancestors of the active folder while "show hidden files" is off.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestLeftPanelExpandCollapse(ExpLorerFreshAppTestCase):
    def test_starts_collapsed_and_tree_button_toggles_it(self) -> None:
        panel = self.app.left_panel
        assert not panel.is_expanded()
        assert not panel.tree_visible()

        panel.tree_button.click()
        wait_until(panel.tree_visible)
        assert panel.is_expanded()

        panel.tree_button.click()
        wait_until(lambda: not panel.tree_visible())
        assert not panel.is_expanded()


class TestFolderTreeFollowsActiveTab(ExpLorerFreshAppTestCase):
    def test_reveals_current_folder_and_follows_navigation(self, tmp_path: Path) -> None:
        outer = tmp_path / "tree_outer"
        inner = outer / "tree_inner"
        inner.mkdir(parents=True)
        tree = self.app.left_panel.tree

        self.app.navigation.navigate_to(str(outer))
        wait_until(lambda: tree.selected_name() == "tree_outer")

        self.app.navigation.navigate_to(str(inner))
        wait_until(lambda: tree.selected_name() == "tree_inner")

    def test_expanding_the_panel_later_catches_up_with_the_current_folder(self, tmp_path: Path) -> None:
        target = tmp_path / "tree_late"
        target.mkdir()

        self.app.navigation.navigate_to(str(target))
        tree = self.app.left_panel.tree

        wait_until(lambda: tree.selected_name() == "tree_late")

    def test_switching_tabs_reveals_the_new_active_tabs_folder(self, tmp_path: Path) -> None:
        first = tmp_path / "tree_first"
        second = tmp_path / "tree_second"
        first.mkdir()
        second.mkdir()
        tree = self.app.left_panel.tree

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(second))
        wait_until(lambda: tree.selected_name() == "tree_second")

        self.app.panes.a.tabs.select(0)
        wait_until(lambda: tree.selected_name() == "tree_first")

    def test_shows_folders_but_not_files(self, tmp_path: Path) -> None:
        parent = tmp_path / "tree_parent"
        (parent / "sub_a").mkdir(parents=True)
        (parent / "sub_b").mkdir()
        (parent / "some_file.txt").write_text("x")
        tree = self.app.left_panel.tree

        # Revealing sub_a expands tree_parent, making all of its children visible rows.
        self.app.navigation.navigate_to(str(parent / "sub_a"))
        wait_until(lambda: tree.selected_name() == "sub_a")

        names = tree.visible_names()
        assert "sub_b" in names
        assert "some_file.txt" not in names


class TestFolderTreeActions(ExpLorerFreshAppTestCase):
    def test_clicking_a_folder_navigates_the_active_tab(self, tmp_path: Path) -> None:
        parent = tmp_path / "tree_click"
        (parent / "alpha").mkdir(parents=True)
        (parent / "beta").mkdir()
        tree = self.app.left_panel.tree

        self.app.navigation.navigate_to(str(parent / "alpha"))
        wait_until(lambda: tree.selected_name() == "alpha")

        tree.click("beta")

        wait_until(lambda: self.app.navigation.address_bar.text == str(parent / "beta"))
        wait_until(lambda: tree.selected_name() == "beta")

    def test_f2_rename_renames_the_folder_and_moves_the_tab_along(self, tmp_path: Path) -> None:
        parent = tmp_path / "tree_rename"
        (parent / "old_name").mkdir(parents=True)
        tree = self.app.left_panel.tree

        self.app.navigation.navigate_to(str(parent / "old_name"))
        wait_until(lambda: tree.selected_name() == "old_name")

        tree.rename("old_name", "new_name")

        wait_until(lambda: (parent / "new_name").is_dir() and not (parent / "old_name").exists())
        # The tab was browsing the renamed folder itself -- it follows the rename.
        wait_until(lambda: self.app.navigation.address_bar.text == str(parent / "new_name"))
        wait_until(lambda: tree.selected_name() == "new_name")
