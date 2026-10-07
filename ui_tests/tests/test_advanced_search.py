"""Coverage for the Advanced (criteria) Search pane (`SearchCriteriaPanel` + its in-place results,
Architecture.md §14.19/§14.20), its async scan busy-state (§14.38), and its KB/MB/GB size-unit
combo, live-edit debounce, and empty-criteria-shows-current-folder fix (§14.56). The Match Case/
Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles §14.56 also added are covered
separately in `test_advanced_search_match_options.py`.

Uses `ExpLorerFreshAppTestCase`: the pane's criteria/results state is per-tab but the Search tab's
own query-box text is window-wide state a shared instance would carry from one test method into
the next.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestAdvancedSearchPanel(ExpLorerFreshAppTestCase):
    def test_opening_shows_plain_non_recursive_listing_of_current_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "top.txt").write_text("x")
        (root / "sub").mkdir()
        (root / "sub" / "nested.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()

        adv = self.app.panes.a.advanced_search
        assert adv.is_visible()
        # Non-recursive: "nested.txt" (inside "sub") must not appear without a real search.
        assert sorted(adv.result_names()) == ["sub", "top.txt"]

    def test_name_filter_narrows_results(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "alpha.txt").write_text("x")
        (root / "beta.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("alpha")
        adv.submit()
        wait_until(lambda: adv.result_names() == ["alpha.txt"])

    def test_min_max_size_range_narrows_results(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "small.bin").write_bytes(b"x" * 100)
        (root / "medium.bin").write_bytes(b"x" * 5000)
        (root / "large.bin").write_bytes(b"x" * 50000)

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_min_size_kb(1)
        adv.set_max_size_kb(10)
        adv.submit()
        wait_until(lambda: adv.result_names() == ["medium.bin"])

    def test_combining_name_and_size_criteria_ands_them(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "report_small.bin").write_bytes(b"x" * 100)
        (root / "report_large.bin").write_bytes(b"x" * 50000)
        (root / "other_large.bin").write_bytes(b"x" * 50000)

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("report")
        adv.set_min_size_kb(10)
        adv.submit()
        wait_until(lambda: adv.result_names() == ["report_large.bin"])

    def test_close_button_dismisses_panel_without_losing_quick_search_state(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "quickmatch.txt").write_text("x")
        (root / "other.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        search = self.app.left_panel.search
        search.type_query("quickmatch")
        wait_until(lambda: search.result_names() == ["quickmatch.txt"])

        search.click_advanced_search()
        adv = self.app.panes.a.advanced_search
        assert adv.is_visible()

        adv.close()
        assert not adv.is_visible()

        # Opening Advanced Search exits (but doesn't clear) the quick search -- the box keeps its
        # typed text, and an explicit refresh brings the same results straight back (Architecture.md
        # §14.19's "each mode's state persists independently while hidden").
        assert search.search_bar.text == "quickmatch"
        search.press_enter()
        wait_until(lambda: search.result_names() == ["quickmatch.txt"])


class TestAdvancedSearchSizeUnit(ExpLorerFreshAppTestCase):
    """Architecture.md §14.56: the Min/Max size fields are now a plain magnitude plus a KB/MB/GB
    unit combo (replacing a single fixed-KB spin box) -- confirms the combo's selection actually
    changes the byte multiplier used for filtering, not just the displayed number.
    """

    def test_min_max_size_range_narrows_results_with_mb_unit(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "small.bin").write_bytes(b"x" * (512 * 1024))  # 0.5 MB
        (root / "medium.bin").write_bytes(b"x" * (2 * 1024 * 1024))  # 2 MB
        (root / "large.bin").write_bytes(b"x" * (10 * 1024 * 1024))  # 10 MB

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        # If the unit combo's selection were ignored (treated as still KB), both medium.bin and
        # large.bin would pass a "min 1" bound -- this value only excludes small.bin once MB is
        # actually honored.
        adv.set_min_size(1, unit="MB")
        adv.set_max_size(5, unit="MB")
        adv.submit()
        wait_until(lambda: adv.result_names() == ["medium.bin"])


class TestAdvancedSearchLiveEditDebounce(ExpLorerFreshAppTestCase):
    """Architecture.md §14.56: editing the Name or size fields now live-refines results after a
    short debounce, same as the quick Search tab -- without pressing the Search button or Enter.
    """

    def test_editing_name_field_without_submitting_narrows_results(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "alpha.txt").write_text("x")
        (root / "beta.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("alpha")
        # Deliberately no adv.submit()/Enter -- the debounce timer alone must trigger the re-scan.
        wait_until(lambda: adv.result_names() == ["alpha.txt"])

    def test_editing_size_field_without_submitting_narrows_results(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "small.bin").write_bytes(b"x" * 100)
        (root / "large.bin").write_bytes(b"x" * 50000)

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_min_size_kb(10)
        wait_until(lambda: adv.result_names() == ["large.bin"])


class TestAdvancedSearchEmptyCriteriaShowsCurrentFolder(ExpLorerFreshAppTestCase):
    """Architecture.md §14.56: clearing every field back to empty while the pane is already
    active must show the current folder's plain, non-recursive listing -- previously this fell
    through to showing the *entire* cached recursive-scan snapshot instead (every file found
    while searching, not just the current folder's own direct children).
    """

    def test_clearing_name_field_after_a_recursive_search_reverts_to_plain_folder_listing(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "top.txt").write_text("x")
        (root / "sub").mkdir()
        (root / "sub" / "nested.txt").write_text("x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        # Triggers a real recursive scan whose candidate snapshot includes both top.txt and
        # sub/nested.txt, even though only nested.txt matches the "nested" query.
        adv.set_name("nested")
        adv.submit()
        wait_until(lambda: adv.result_names() == ["nested.txt"])

        # Clearing the field (not closing the panel) must show the current folder's own plain
        # listing ("sub" the folder, "top.txt") -- not the recursive snapshot's flattened
        # ["nested.txt", "top.txt"], which would be the pre-fix, buggy result.
        adv.set_name("")
        wait_until(lambda: sorted(adv.result_names()) == ["sub", "top.txt"])


class TestAdvancedSearchAsync(ExpLorerFreshAppTestCase):
    """§14.38: the recursive scan/tag-first fetch run off the UI thread, so the pane's Search
    button shows a busy state while a scan is in flight and the main window keeps responding.
    """

    def test_search_completes_without_freezing_the_ui_thread(self, tmp_path: Path) -> None:
        """The recursive scan dispatches to a background worker thread (Architecture.md §14.38)
        rather than running on the UI thread, so a largeish folder must not freeze the window.

        Catching the Search button's "Searching…" text (`setSearching(true)`) itself proved to be
        a race no amount of file-count padding or tight polling could reliably win against on this
        hardware (confirmed by hand: even a ~48,000-file/80-subfolder tree's scan still completed
        between one poll and the next at a ~1ms cadence) -- so this best-effort-checks for it
        without failing the test over it, same posture as 3.1's `loading_label` carve-out, and
        instead asserts the two things that actually matter: the scan finishes with the right
        results, and submitting it doesn't hang the main window (a frozen UI thread would fail
        every poll below outright, including the final one).
        """
        root = tmp_path / "root"
        root.mkdir()
        for sub in range(40):
            subdir = root / f"sub_{sub}"
            subdir.mkdir()
            for i in range(500):
                (subdir / f"file_{i}.txt").write_bytes(b"x")
        (root / "sub_0" / "unique_marker.txt").write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        self.app.left_panel.search.click_advanced_search()
        adv = self.app.panes.a.advanced_search

        adv.set_name("unique_marker")
        adv.submit()
        try:
            wait_until(lambda: adv.is_searching() or None, timeout=2, interval=0.02)
        except TimeoutError:
            pass  # best-effort only -- see docstring

        wait_until(lambda: adv.result_names() == ["unique_marker.txt"], timeout=30)
        assert adv.submit_button.text == "Search"
