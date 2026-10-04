"""Coverage for the quick Search tab's plain recursive filename search (SearchPanelWidget,
Architecture.md §14.18/§14.42) and its follow-up UX fixes: selection sync to the main pane,
Enter-forces-refresh, refresh-after-navigate (§14.43), and the quick-search debounce (§14.46).

Uses `ExpLorerFreshAppTestCase`: the search panel's query/results are window-wide state a shared
instance would carry from one test method into the next.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.details_view import read_details_rows
from framework.wait import wait_until


class TestSearchRecursiveMatching(ExpLorerFreshAppTestCase):
    def test_matches_files_several_levels_deep(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "a" / "b" / "c").mkdir(parents=True)
        (root / "needle.txt").write_text("x")
        (root / "a" / "needle_nested.txt").write_text("x")
        (root / "a" / "b" / "c" / "needle_deep.txt").write_text("x")
        (root / "a" / "unrelated.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("needle")

        wait_until(
            lambda: set(search.result_names()) == {"needle.txt", "needle_nested.txt", "needle_deep.txt"}
        )

    def test_toggling_list_and_tree_view_preserves_match_set(self, tmp_path: Path) -> None:
        # Flat matches (no subfolders): the search tree view renders each *visible* row as a flat
        # child of the tree widget (same convention as the folder tree, automation_id.py's
        # `FolderTree` docstring) -- a match inside a collapsed ancestor folder wouldn't show up
        # without first expanding it, which is no part of what this toggle itself is covering.
        root = tmp_path / "root"
        root.mkdir()
        (root / "match_one.txt").write_text("x")
        (root / "match_two.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: set(search.result_names()) == {"match_one.txt", "match_two.txt"})

        assert not search.is_tree_view()
        search.toggle_view_as_tree()
        assert search.is_tree_view()
        wait_until(lambda: set(search.result_names()) == {"match_one.txt", "match_two.txt"})

        search.toggle_view_as_tree()
        assert not search.is_tree_view()
        wait_until(lambda: set(search.result_names()) == {"match_one.txt", "match_two.txt"})

    def test_collapse_all_collapses_a_matched_folders_subtree(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match.txt"])

        search.toggle_view_as_tree()
        # The match's ancestor folder ("sub") shows collapsed by default -- its own matched child
        # isn't part of the flat visible-rows stream until expanded (same convention as the
        # folder tree). Collapse All is then a no-op here, which is itself the point: it must
        # leave an already-collapsed tree showing just that one ancestor row, not error out or
        # blank the view.
        wait_until(lambda: search.result_names() == ["sub"])
        search.click_collapse_all()
        wait_until(lambda: search.result_names() == ["sub"])

    def test_double_click_navigates_owning_tab_to_results_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match.txt"])

        search.double_click("match.txt")
        wait_until(lambda: self.app.navigation.address_bar.text == str(root / "sub"))

    def test_clear_empties_results_and_query_box(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match.txt"])

        search.click_clear()
        wait_until(lambda: search.search_bar.text == "")
        assert search.result_names() == []

    def test_refresh_re_runs_the_same_query(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match_one.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match_one.txt"])

        (root / "match_two.txt").write_text("x")
        search.click_refresh()
        wait_until(lambda: set(search.result_names()) == {"match_one.txt", "match_two.txt"})


class TestSearchSelectionSync(ExpLorerFreshAppTestCase):
    def test_selecting_a_result_resolves_as_the_main_panes_selection(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("distinctive preview content")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match.txt"])

        search.select("match.txt")
        preview = self.app.right_panel.preview
        wait_until(lambda: preview.text_view.exists() and preview.text_view.text == "distinctive preview content")


class TestSearchEnterForcesRefresh(ExpLorerFreshAppTestCase):
    def test_enter_re_scans_even_without_text_changing(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match_one.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match_one.txt"])

        # Further typing only re-filters the cached snapshot (no new disk I/O), so a file added
        # after the initial scan doesn't show up on its own -- Enter forces a fresh scan.
        (root / "match_two.txt").write_text("x")
        search.press_enter()
        wait_until(lambda: set(search.result_names()) == {"match_one.txt", "match_two.txt"})


class TestSearchSurvivesNavigationAway(ExpLorerFreshAppTestCase):
    def test_navigating_away_keeps_query_text_and_enter_re_roots_the_search(self, tmp_path: Path) -> None:
        here = tmp_path / "here"
        here.mkdir()
        (here / "match.txt").write_text("from here")
        elsewhere = tmp_path / "elsewhere"
        elsewhere.mkdir()
        (elsewhere / "match.txt").write_text("from elsewhere")

        self.app.navigation.navigate_to(str(here))
        search = self.app.left_panel.search
        search.type_query("match")
        wait_until(lambda: search.result_names() == ["match.txt"])

        self.app.navigation.navigate_to(str(elsewhere))

        # TabViewModel::navigateTo always exits the active search (Architecture.md §14.43), but
        # leaves the Search tab's own query box untouched.
        assert search.search_bar.text == "match"

        search.press_enter()
        wait_until(lambda: search.result_names() == ["match.txt"])
        preview = self.app.right_panel.preview
        search.select("match.txt")
        wait_until(lambda: preview.text_view.exists() and preview.text_view.text == "from elsewhere")


class TestSearchDebounce(ExpLorerFreshAppTestCase):
    def test_typed_query_eventually_settles_on_the_correct_filtered_set(self, tmp_path: Path) -> None:
        """§14.46 debounces quick-search filtering by 500ms so a large candidate list isn't
        re-filtered on every keystroke. Directly observing "only one refresh, not one per
        keystroke" via black-box timing turned out to be impractical here (confirmed by hand:
        reading `result_names()` through UI Automation alone routinely takes longer than the
        500ms debounce window, so a post-keystroke poll can never reliably land inside it) -- this
        instead confirms the functionally-observable guarantee the debounce must preserve: typing
        a multi-character query character-by-character still lands on the exact right filtered
        set, not a stale intermediate one.
        """
        root = tmp_path / "root"
        root.mkdir()
        (root / "alpha.txt").write_text("x")
        (root / "alphabet.txt").write_text("x")
        (root / "beta.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("alpha")

        wait_until(lambda: set(search.result_names()) == {"alpha.txt", "alphabet.txt"})
