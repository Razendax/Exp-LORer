"""Coverage for the Advanced Search pane's own Match Case/Whole Word/Exact Name/Match Regex/Skip
Git-Ignored toggles (`SearchCriteriaPanel`, Architecture.md §14.56) -- a port of the quick Search
tab's equivalent 5 buttons (see `test_search_match_options.py`) onto the criteria panel, with its
own distinct button instances/object names so both panels can be visible at once.

Uses `ExpLorerFreshAppTestCase`: the criteria panel's toggle state is per-tab state a shared
instance would carry from one test method into the next.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestAdvancedSearchMatchCaseToggle(ExpLorerFreshAppTestCase):
    def test_match_case_narrows_and_widens_results(self, tmp_path: Path) -> None:
        root = tmp_path / "match_case_root"
        root.mkdir()
        (root / "File.txt").write_text("x")
        (root / "file_other.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("File")
        adv.submit()
        wait_until(lambda: set(adv.result_names()) == {"File.txt", "file_other.txt"})

        adv.match_case_button.click()
        wait_until(lambda: adv.result_names() == ["File.txt"])

        adv.match_case_button.click()
        wait_until(lambda: set(adv.result_names()) == {"File.txt", "file_other.txt"})


class TestAdvancedSearchMatchModeExclusiveOptional(ExpLorerFreshAppTestCase):
    def test_whole_word_toggle_narrows_then_reverts_to_contains_on_second_click(self, tmp_path: Path) -> None:
        root = tmp_path / "whole_word_root"
        root.mkdir()
        (root / "File Name.txt").write_text("x")
        (root / "FileName.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("File")
        adv.submit()
        wait_until(lambda: set(adv.result_names()) == {"File Name.txt", "FileName.txt"})

        adv.whole_word_button.click()
        wait_until(lambda: adv.result_names() == ["File Name.txt"])
        assert adv.whole_word_button.is_checked()

        # Clicking the already-active button again reverts to the default Contains behavior
        # (the hand-rolled "exclusive but deselectable" logic ported from SearchPanelWidget).
        adv.whole_word_button.click()
        wait_until(lambda: set(adv.result_names()) == {"File Name.txt", "FileName.txt"})
        assert not adv.whole_word_button.is_checked()

    def test_exact_name_and_regex_are_mutually_exclusive_with_whole_word(self, tmp_path: Path) -> None:
        root = tmp_path / "exclusive_root"
        root.mkdir()
        (root / "File.txt").write_text("x")
        (root / "File Name.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("File")
        adv.submit()

        adv.whole_word_button.click()
        wait_until(lambda: adv.whole_word_button.is_checked())

        adv.exact_name_button.click()
        wait_until(lambda: adv.exact_name_button.is_checked())
        assert not adv.whole_word_button.is_checked()

        adv.regex_button.click()
        wait_until(lambda: adv.regex_button.is_checked())
        assert not adv.exact_name_button.is_checked()


class TestAdvancedSearchSkipGitIgnoredToggle(ExpLorerFreshAppTestCase):
    def test_skip_git_ignored_excludes_and_reincludes_ignored_entries(self, tmp_path: Path) -> None:
        root = tmp_path / "skip_git_ignored_root"
        root.mkdir()
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        (root / ".gitignore").write_text("ignored_match.txt\n")
        (root / "ignored_match.txt").write_text("x")
        (root / "kept_match.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("match")
        adv.submit()
        wait_until(lambda: set(adv.result_names()) == {"ignored_match.txt", "kept_match.txt"})

        # Toggling Skip Git-Ignored changes what the recursive walk *visits* (Architecture.md
        # §14.56), so it must force a rescan -- a stale cached snapshot from the un-pruned walk
        # above would otherwise still show ignored_match.txt.
        adv.skip_git_ignored_button.click()
        wait_until(lambda: adv.result_names() == ["kept_match.txt"])
        assert adv.skip_git_ignored_button.is_checked()

        adv.skip_git_ignored_button.click()
        wait_until(lambda: set(adv.result_names()) == {"ignored_match.txt", "kept_match.txt"})
        assert not adv.skip_git_ignored_button.is_checked()
