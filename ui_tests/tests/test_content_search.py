"""Coverage for the left panel's "Find in Files" tab (ContentSearchPanelWidget, Architecture.md
§14.104): content (not filename) matching, binary-file skipping, line-row grouping, the Match
Case/Whole Word/Regex/Skip Git-Ignored toggles, the Include/Exclude "Search Details" fields, and
result selection/navigation.

Uses `ExpLorerFreshAppTestCase`: the panel's query/options/results are window-wide state a shared
instance would carry from one test method into the next.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestContentSearchBasicMatching(ExpLorerFreshAppTestCase):
    def test_matches_content_several_levels_deep(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "a" / "b").mkdir(parents=True)
        (root / "needle.txt").write_text("the needle is here")
        (root / "a" / "nested.txt").write_text("found needle inside")
        (root / "a" / "b" / "deep.txt").write_text("needle again")
        (root / "a" / "other.txt").write_text("nothing interesting")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: set(content_search.file_names()) == {"needle.txt", "nested.txt", "deep.txt"})

    def test_matches_by_content_not_by_filename(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        # Filename contains the query but content doesn't -- must NOT match.
        (root / "needle.txt").write_text("unrelated text")
        # Filename doesn't contain the query but content does -- must match.
        (root / "other.txt").write_text("needle inside here")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["other.txt"])

    def test_binary_files_are_skipped(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "text_match.txt").write_text("needle here")
        (root / "binary_match.dat").write_bytes(b"needle\x00\x01\x02binary")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["text_match.txt"])


class TestContentSearchLineRows(ExpLorerFreshAppTestCase):
    def test_shows_matching_lines_grouped_under_their_file(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "multi.txt").write_text("line one\nneedle on line two\nline three\nneedle again on line four\n")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["multi.txt"])
        wait_until(
            lambda: content_search.line_texts()
            == ["2:  needle on line two", "4:  needle again on line four"]
        )

    def test_collapse_all_hides_line_rows_expand_all_restores_them(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "multi.txt").write_text("needle one\nneedle two\n")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: len(content_search.line_texts()) == 2)

        content_search.click_collapse_all()
        wait_until(lambda: content_search.line_texts() == [])
        assert content_search.file_names() == ["multi.txt"]

        content_search.click_expand_all()
        wait_until(lambda: len(content_search.line_texts()) == 2)


class TestContentSearchMatchCaseToggle(ExpLorerFreshAppTestCase):
    def test_match_case_narrows_and_widens_results(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "upper.txt").write_text("Needle here")
        (root / "lower.txt").write_text("needle here")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("Needle")

        wait_until(lambda: set(content_search.file_names()) == {"upper.txt", "lower.txt"})

        content_search.match_case_button.click()
        wait_until(lambda: content_search.file_names() == ["upper.txt"])

        content_search.match_case_button.click()
        wait_until(lambda: set(content_search.file_names()) == {"upper.txt", "lower.txt"})


class TestContentSearchMatchModeExclusiveOptional(ExpLorerFreshAppTestCase):
    def test_whole_word_toggle_narrows_then_reverts_to_contains_on_second_click(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "word.txt").write_text("the cat sat")
        (root / "compound.txt").write_text("concatenate this")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("cat")

        wait_until(lambda: set(content_search.file_names()) == {"word.txt", "compound.txt"})

        content_search.whole_word_button.click()
        wait_until(lambda: content_search.file_names() == ["word.txt"])
        assert content_search.whole_word_button.is_checked()

        # Clicking the already-active button again reverts to the default Contains behavior.
        content_search.whole_word_button.click()
        wait_until(lambda: set(content_search.file_names()) == {"word.txt", "compound.txt"})
        assert not content_search.whole_word_button.is_checked()

    def test_regex_is_mutually_exclusive_with_whole_word(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "word.txt").write_text("the cat sat")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("cat")

        content_search.whole_word_button.click()
        wait_until(lambda: content_search.whole_word_button.is_checked())

        content_search.regex_button.click()
        wait_until(lambda: content_search.regex_button.is_checked())
        assert not content_search.whole_word_button.is_checked()

    def test_regex_mode_matches_pattern(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "one.txt").write_text("foo123bar")
        (root / "two.txt").write_text("foobar")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.regex_button.click()
        wait_until(lambda: content_search.regex_button.is_checked())

        # Avoids a bare "+" in the typed pattern -- pywinauto's `type_keys` parses "+" as a Shift
        # modifier prefix for whatever follows, so a literal "+" can't be typed this way.
        content_search.type_query(r"foo\d\d\dbar")

        wait_until(lambda: content_search.file_names() == ["one.txt"])


class TestContentSearchSkipGitIgnoredToggle(ExpLorerFreshAppTestCase):
    def test_skip_git_ignored_excludes_and_reincludes_ignored_entries(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        (root / ".gitignore").write_text("ignored.txt\n")
        (root / "ignored.txt").write_text("needle text")
        (root / "kept.txt").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        # ContentSearchOptions::skipGitIgnored defaults to true (an unfiltered content scan reads
        # every candidate's bytes, not just its name, so it's drastically more expensive over a
        # typical git-ignored build/vcpkg_installed tree) -- so "ignored.txt" starts excluded.
        wait_until(lambda: content_search.file_names() == ["kept.txt"])
        assert content_search.skip_git_ignored_button.is_checked()

        content_search.skip_git_ignored_button.click()
        wait_until(lambda: set(content_search.file_names()) == {"ignored.txt", "kept.txt"})
        assert not content_search.skip_git_ignored_button.is_checked()

        content_search.skip_git_ignored_button.click()
        wait_until(lambda: content_search.file_names() == ["kept.txt"])
        assert content_search.skip_git_ignored_button.is_checked()


class TestContentSearchDetailsDisclosure(ExpLorerFreshAppTestCase):
    def test_search_details_are_hidden_by_default_and_the_disclosure_toggles_them(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search

        assert not content_search.is_details_visible()
        content_search.toggle_details()
        assert content_search.is_details_visible()
        content_search.toggle_details()
        assert not content_search.is_details_visible()


class TestContentSearchIncludeExcludeFields(ExpLorerFreshAppTestCase):
    def test_include_pattern_narrows_results_to_matching_paths(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match.txt").write_text("needle text")
        (root / "match.md").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: set(content_search.file_names()) == {"match.txt", "match.md"})

        content_search.set_include("*.txt")
        wait_until(lambda: content_search.file_names() == ["match.txt"])

    def test_exclude_pattern_removes_matching_paths(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match.txt").write_text("needle text")
        (root / "match.md").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: set(content_search.file_names()) == {"match.txt", "match.md"})

        content_search.set_exclude("*.md")
        wait_until(lambda: content_search.file_names() == ["match.txt"])


class TestContentSearchResultActions(ExpLorerFreshAppTestCase):
    def test_double_click_header_row_navigates_owning_tab_to_results_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["match.txt"])

        content_search.double_click(content_search.header_texts()[0])
        wait_until(lambda: self.app.navigation.address_bar.text == str(root / "sub"))

    def test_double_click_line_row_also_navigates_to_the_files_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("needle one\nneedle two\n")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: len(content_search.line_texts()) == 2)

        content_search.double_click(content_search.line_texts()[0])
        wait_until(lambda: self.app.navigation.address_bar.text == str(root / "sub"))

    def test_selecting_a_result_resolves_as_the_main_panes_selection(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        (root / "sub").mkdir(parents=True)
        (root / "sub" / "match.txt").write_text("distinctive preview content with needle")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["match.txt"])

        content_search.select(content_search.header_texts()[0])
        preview = self.app.right_panel.preview
        wait_until(
            lambda: preview.text_view.exists()
            and preview.text_view.text == "distinctive preview content with needle"
        )


class TestContentSearchClearAndRefresh(ExpLorerFreshAppTestCase):
    def test_clear_empties_results_and_query_box(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "match.txt").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["match.txt"])

        content_search.click_clear()
        wait_until(lambda: content_search.search_bar.text == "")
        assert content_search.file_names() == []

    def test_refresh_re_scans_disk_for_content_added_since_the_last_scan(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "first.txt").write_text("needle text")

        self.app.navigation.navigate_to(str(root))
        content_search = self.app.left_panel.content_search
        content_search.type_query("needle")

        wait_until(lambda: content_search.file_names() == ["first.txt"])

        # Content search always rescans on a settled query-text edit, but a file added without
        # the query text itself changing needs an explicit Refresh/Enter to show up.
        (root / "second.txt").write_text("needle text too")
        content_search.click_refresh()
        wait_until(lambda: set(content_search.file_names()) == {"first.txt", "second.txt"})
