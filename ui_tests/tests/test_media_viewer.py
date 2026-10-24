"""Coverage for Media Viewer mode (Architecture.md §14.61/§14.62): entering/exiting via the View
menu's checkable "Media Viewer Mode" action and the Searcher panel's "Exit viewer mode" button, the
locked icon-grid listing every image/video found recursively under whatever folder the tab was
showing when the mode was entered, the Images/Videos kind switch, the AND-combined tag and name
filters exposed by the right panel's "Searcher" tab, the rest of the pane's toolbar/other tabs being
locked for the mode's duration, and the left/right side panels being swapped out/back in around it.

Thumbnail/metadata-overlay rendering itself (§14.62) isn't covered here -- pywinauto/UI Automation
exposes row *names* but not a delegate's painted pixels, so only which files appear in the grid is
assertable, the same carve-out `test_advanced_search.py`'s highlight-styling note documents.

Uses `ExpLorerFreshAppTestCase`: Media Viewer's root/kind/tag-and-name filters are per-tab state, so
a shared instance would carry a previous test method's tab state into the next one's.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.details_view import read_name_column, select_row
from framework.element import Element
from framework.wait import wait_until


def _is_hidden(element: Element) -> bool:
    """True once `element` is gone from view -- `SidePanelWidget::setPanelTabButtonVisible`
    hides a header button with plain `QWidget::setVisible(false)` rather than removing it, and
    Qt's `uia` bridge keeps a merely-invisible widget enumerable (confirmed by `AdvancedSearchPanel.
    is_visible()`'s own exists()-then-is_visible() pattern) -- so a bare `not element.exists()`
    would be the wrong check here; this treats "no longer findable at all" as hidden too, for
    robustness against a bridge that *does* fully drop it.
    """
    try:
        return not element.raw.is_visible()
    except LookupError:
        return True


def _make_media_tree(root: Path) -> None:
    root.mkdir()
    (root / "top.jpg").write_bytes(b"x")
    (root / "notes.txt").write_text("not media")
    sub = root / "sub"
    sub.mkdir()
    (sub / "nested.png").write_bytes(b"x")
    (sub / "clip.mp4").write_bytes(b"x")


class TestMediaViewerModeEntryAndExit(ExpLorerFreshAppTestCase):
    def test_entering_shows_recursive_image_grid_and_locks_the_pane(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        _make_media_tree(root)

        self.app.navigation.navigate_to(str(root))
        self.app.view_menu.toggle_media_viewer_mode()

        # Recursive, image-only by default (notes.txt and sub/clip.mp4 excluded), even though
        # nested.png lives two levels deep under the root the tab was showing when entered.
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["nested.png", "top.jpg"])

        # Navigation/view-mode toolbar is locked for the mode's duration (Architecture.md §14.61 --
        # the grid's root can't be re-scoped mid-session).
        assert not self.app.navigation.address_bar.is_enabled()
        assert not self.app.navigation.back.is_enabled()
        assert not self.app.navigation.forward.is_enabled()
        assert not self.app.navigation.up.is_enabled()
        assert not self.app.navigation.refresh.is_enabled()
        assert not self.app.navigation.view_mode.is_enabled()

        # Left panel hides outright; right panel shows only the Searcher tab, forced active.
        assert not self.app.left_panel.is_visible()
        assert self.app.right_panel.searcher_button.is_checked()
        assert _is_hidden(self.app.right_panel.tags_button)
        assert _is_hidden(self.app.right_panel.preview_button)
        assert _is_hidden(self.app.right_panel.monitoring_button)
        assert self.app.right_panel.searcher.is_visible()

    def test_exit_button_restores_normal_browsing_and_unlocks_the_pane(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        _make_media_tree(root)

        self.app.navigation.navigate_to(str(root))
        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["nested.png", "top.jpg"])

        self.app.right_panel.searcher.exit()

        wait_until(lambda: self.app.navigation.address_bar.is_enabled())
        assert self.app.navigation.view_mode.is_enabled()
        assert self.app.left_panel.is_visible()
        assert _is_hidden(self.app.right_panel.searcher_button)
        assert self.app.right_panel.tags_button.exists()

        # Back to plain, non-recursive browsing of the root -- "sub" the folder itself, not its
        # nested.png, which only showed up through the grid's recursive scan.
        assert self.app.navigation.address_bar.text == str(root)
        wait_until(lambda: sorted(read_name_column(self.app.panes.a.file_tree_view)) == ["notes.txt", "sub", "top.jpg"])

    def test_retoggling_the_view_menu_action_exits_too(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        _make_media_tree(root)

        self.app.navigation.navigate_to(str(root))
        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: self.app.right_panel.searcher.is_visible())

        self.app.view_menu.toggle_media_viewer_mode()

        wait_until(lambda: self.app.navigation.address_bar.is_enabled())
        assert _is_hidden(self.app.right_panel.searcher_button)
        assert self.app.left_panel.is_visible()


class TestMediaViewerModeTabIsolation(ExpLorerFreshAppTestCase):
    def test_other_tabs_in_the_pane_are_disabled_while_active(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        self.app.navigation.navigate_to(str(first))
        self.app.navigation.new_tab.click()
        self.app.navigation.navigate_to(str(second))
        wait_until(lambda: self.app.panes.a.tabs.titles() == ["first", "second"])

        self.app.panes.a.tabs.select(0)
        wait_until(lambda: self.app.navigation.address_bar.text == str(first))

        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: self.app.right_panel.searcher.is_visible())

        assert self.app.panes.a.tabs.is_enabled(0)  # the tab that entered the mode stays usable

        # `setTabEnabled(1, false)` doesn't round-trip to the TabItem's UIA IsEnabled property
        # (confirmed by hand: is_enabled(1) still reads True here) -- so disabledness is checked
        # functionally instead: clicking it must do nothing, stranding its fixed grid root is
        # avoided by it being genuinely unreachable, not just reported as such.
        self.app.panes.a.tabs.select(1)
        assert self.app.right_panel.searcher.is_visible()
        assert self.app.navigation.address_bar.text == str(first)

        self.app.right_panel.searcher.exit()
        wait_until(lambda: self.app.navigation.address_bar.is_enabled())

        assert self.app.panes.a.tabs.is_enabled(0)
        assert self.app.panes.a.tabs.is_enabled(1)


class TestMediaViewerModeKindFilter(ExpLorerFreshAppTestCase):
    def test_switching_kind_combo_swaps_between_images_and_videos(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "photo.jpg").write_bytes(b"x")
        (root / "movie.mp4").write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: self.app.panes.a.media_viewer_result_names() == ["photo.jpg"])

        self.app.right_panel.searcher.select_kind("Videos")
        wait_until(lambda: self.app.panes.a.media_viewer_result_names() == ["movie.mp4"])

        self.app.right_panel.searcher.select_kind("Images")
        wait_until(lambda: self.app.panes.a.media_viewer_result_names() == ["photo.jpg"])


class TestMediaViewerModeNameFilter(ExpLorerFreshAppTestCase):
    def test_name_filter_narrows_the_grid(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "alpha.jpg").write_bytes(b"x")
        (root / "beta.jpg").write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["alpha.jpg", "beta.jpg"])

        self.app.right_panel.searcher.set_name_filter("alpha")
        wait_until(lambda: self.app.panes.a.media_viewer_result_names() == ["alpha.jpg"])

        self.app.right_panel.searcher.set_name_filter("")
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["alpha.jpg", "beta.jpg"])


class TestMediaViewerModeTagFilter(ExpLorerFreshAppTestCase):
    def test_tag_filter_narrows_the_grid_and_can_be_removed(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "untagged.jpg").write_bytes(b"x")
        (root / "tagged.jpg").write_bytes(b"x")

        self.app.navigation.navigate_to(str(root))
        wait_until(lambda: self.app.navigation.address_bar.text == str(root))
        select_row(self.app.panes.a.file_tree_view, "tagged.jpg")
        self.app.right_panel.tags.create_and_apply_tag("Vacation")
        wait_until(lambda: self.app.right_panel.tags.selected_item_tags() == ["Vacation"])

        self.app.view_menu.toggle_media_viewer_mode()
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["tagged.jpg", "untagged.jpg"])

        searcher = self.app.right_panel.searcher
        searcher.add_tag_filter("Vacation")
        wait_until(lambda: searcher.applied_filter_names() == ["Vacation"])
        wait_until(lambda: self.app.panes.a.media_viewer_result_names() == ["tagged.jpg"])

        searcher.remove_tag_filter("Vacation")
        wait_until(lambda: searcher.applied_filter_names() == [])
        wait_until(lambda: sorted(self.app.panes.a.media_viewer_result_names()) == ["tagged.jpg", "untagged.jpg"])
