"""Coverage for the quick Search tab's Match Case/Whole Word/Exact Name/Match Regex toggles
(SearchPanelWidget, Architecture.md §14.44): Match Case narrowing/widening which results show up
for a differently-cased query, and the ExclusiveOptional behavior of the Whole Word/Exact Name/
Regex group -- clicking the already-active one again reverts to the default (Contains) behavior.

Uses `ExpLorerFreshAppTestCase`: the search panel's toggle state is window-wide state a shared
instance would carry from one test method into the next.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestSearchMatchCaseToggle(ExpLorerFreshAppTestCase):
    def test_match_case_narrows_and_widens_results(self, tmp_path: Path) -> None:
        root = tmp_path / "match_case_root"
        (root / "File.txt").parent.mkdir(parents=True, exist_ok=True)
        (root / "File.txt").write_text("x")
        (root / "file_other.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("File")

        wait_until(lambda: set(search.result_names()) == {"File.txt", "file_other.txt"})

        search.match_case_button.click()
        wait_until(lambda: search.result_names() == ["File.txt"])

        search.match_case_button.click()
        wait_until(lambda: set(search.result_names()) == {"File.txt", "file_other.txt"})


class TestSearchMatchModeExclusiveOptional(ExpLorerFreshAppTestCase):
    def test_whole_word_toggle_narrows_then_reverts_to_contains_on_second_click(self, tmp_path: Path) -> None:
        root = tmp_path / "whole_word_root"
        root.mkdir(parents=True)
        (root / "File Name.txt").write_text("x")
        (root / "FileName.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("File")

        wait_until(lambda: set(search.result_names()) == {"File Name.txt", "FileName.txt"})

        search.whole_word_button.click()
        wait_until(lambda: search.result_names() == ["File Name.txt"])
        assert search.whole_word_button.is_checked()

        # Clicking the already-active button again reverts to the default Contains behavior
        # (QButtonGroup::ExclusionPolicy::ExclusiveOptional).
        search.whole_word_button.click()
        wait_until(lambda: set(search.result_names()) == {"File Name.txt", "FileName.txt"})
        assert not search.whole_word_button.is_checked()

    def test_exact_name_and_regex_are_mutually_exclusive_with_whole_word(self, tmp_path: Path) -> None:
        root = tmp_path / "exclusive_root"
        root.mkdir(parents=True)
        (root / "File.txt").write_text("x")
        (root / "File Name.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("File")

        search.whole_word_button.click()
        wait_until(lambda: search.whole_word_button.is_checked())

        search.exact_name_button.click()
        wait_until(lambda: search.exact_name_button.is_checked())
        assert not search.whole_word_button.is_checked()

        search.regex_button.click()
        wait_until(lambda: search.regex_button.is_checked())
        assert not search.exact_name_button.is_checked()
