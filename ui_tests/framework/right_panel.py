"""Page objects for the right SidePanelWidget's tab switcher (Tags / Preview / Monitoring /
Searcher)."""

from __future__ import annotations

from typing import Any, Callable, List

from .automation_id import AutomationID
from .element import Element
from .tag_chip import chip_names, find_chip
from .wait import wait_until


class TagPanel:
    def __init__(self, container: Callable[[], object]):
        self.search_edit = Element(container, AutomationID.TAG_PANEL_SEARCH_EDIT)
        self.create_tag_button = Element(container, AutomationID.TAG_PANEL_CREATE_TAG_BUTTON)
        self.folder_tags_section = Element(container, AutomationID.TAG_PANEL_FOLDER_TAGS_SECTION)
        self.search_matches_section = Element(container, AutomationID.TAG_PANEL_SEARCH_MATCHES_SECTION)
        self.selected_item_tags_section = Element(container, AutomationID.TAG_PANEL_SELECTED_ITEM_TAGS_SECTION)

    def folder_tags(self) -> list:
        """Chip names under "Folder tags" -- the browsed folder's own tags plus its tagged
        ancestors' (TagListViewModel::tagsForPathWithAncestors), outermost-first.
        """
        return chip_names(self.folder_tags_section.raw)

    def selected_item_tags(self) -> list:
        """Chip names under "Tags on selection" -- the resolved tagging target(s)' tags
        (TagListViewModel::resolveTargets); with multiple items selected this is the intersection
        (tags common to all of them), and it's empty when nothing resolves (This PC root with
        nothing selected, or a selection made entirely of archive-nested entries).
        """
        return chip_names(self.selected_item_tags_section.raw)

    def search_matches(self) -> list:
        """Chip names under "Matching tags" for whatever query is currently in `search_edit`."""
        return chip_names(self.search_matches_section.raw)

    def search_for(self, query: str) -> None:
        self.search_edit.click()
        self.search_edit.set_text(query)

    def create_and_apply_tag(self, name: str) -> None:
        """Types `name` into the search box and clicks the inline "Create tag" affordance --
        creates a brand-new tag and immediately assigns it to the resolved target
        (TagListViewModel::createAndAddTagFromQuery).
        """
        self.search_for(name)
        wait_until(lambda: self.create_tag_button.exists())
        self.create_tag_button.click()


class PreviewPanel:
    def __init__(self, container: Callable[[], object]):
        self.image_label = Element(container, AutomationID.PREVIEW_IMAGE_LABEL)
        self.text_view = Element(container, AutomationID.PREVIEW_TEXT_VIEW)
        self.folder_list = Element(container, AutomationID.PREVIEW_FOLDER_LIST)
        self.loading_label = Element(container, AutomationID.PREVIEW_LOADING_LABEL)
        self.unsupported_label = Element(container, AutomationID.PREVIEW_UNSUPPORTED_LABEL)
        self.error_label = Element(container, AutomationID.PREVIEW_ERROR_LABEL)


class MonitoringPanel:
    def __init__(self, container: Callable[[], object]):
        self.start_monitoring = Element(container, AutomationID.MONITORING_START_STOP_BUTTON)
        self.pin = Element(container, AutomationID.MONITORING_PIN_BUTTON)
        self.log_view = Element(container, AutomationID.MONITORING_LOG_VIEW)
        self.total_label = Element(container, AutomationID.MONITORING_TOTAL_LABEL)


class MediaViewerSearchPanel:
    """The right SidePanelWidget's "Searcher" tab (`MediaViewerSearchPanelWidget.cpp`,
    Architecture.md §14.61) -- the only UI for kind-switching/tag-and-name-filtering/exiting Media
    Viewer mode while it's active on the focused pane's current tab.

    Unlike Tags/Preview/Monitoring, this tab's header button is only ever visible while Media
    Viewer mode is active (`MainWindow::onMediaViewerModeChanged` hides the other 3 buttons and
    shows + force-activates this one) -- so, unlike `RightPanel.tags`/`.preview`/`.monitoring`,
    `RightPanel.searcher` below does not click the button itself; enter the mode first (`app.
    view_menu.toggle_media_viewer_mode()`), which activates this tab automatically.
    """

    def __init__(self, container: Callable[[], object]):
        self._panel = Element(container, AutomationID.MEDIA_VIEWER_SEARCH_PANEL)
        self.kind_combo = Element(container, AutomationID.MEDIA_VIEWER_KIND_COMBO)
        self.exit_button = Element(container, AutomationID.MEDIA_VIEWER_EXIT_BUTTON)
        self.loading_label = Element(container, AutomationID.MEDIA_VIEWER_LOADING_LABEL)
        self.applied_filters_section = Element(container, AutomationID.MEDIA_VIEWER_APPLIED_FILTERS_SECTION)
        self.tag_search_edit = Element(container, AutomationID.MEDIA_VIEWER_TAG_SEARCH_EDIT)
        self.tag_matches_section = Element(container, AutomationID.MEDIA_VIEWER_TAG_MATCHES_SECTION)
        self.match_case_button = Element(container, AutomationID.MEDIA_VIEWER_MATCH_CASE_BUTTON)
        self.whole_word_button = Element(container, AutomationID.MEDIA_VIEWER_WHOLE_WORD_BUTTON)
        self.exact_name_button = Element(container, AutomationID.MEDIA_VIEWER_EXACT_NAME_BUTTON)
        self.regex_button = Element(container, AutomationID.MEDIA_VIEWER_REGEX_BUTTON)
        self.skip_git_ignored_button = Element(container, AutomationID.MEDIA_VIEWER_SKIP_GIT_IGNORED_BUTTON)
        self.name_edit = Element(container, AutomationID.MEDIA_VIEWER_NAME_EDIT)

    def is_visible(self) -> bool:
        try:
            return self._panel.exists(timeout=0.5) and bool(self._panel.raw.is_visible())
        except LookupError:
            return False

    def select_kind(self, kind: str) -> None:
        """Switches the Images/Videos combo (`MediaViewerKind`) -- `kind` is "Images" or "Videos"."""
        from .combo_box import select_combo_item

        select_combo_item(self.kind_combo.raw, kind, ["Images", "Videos"])

    def applied_filter_names(self) -> List[str]:
        """Tag names currently in the "Tag filters:" row (`TagChipWidget::Kind::Removable`)."""
        return chip_names(self.applied_filters_section.raw)

    def tag_matches(self) -> List[str]:
        """Tag names currently in the matching-tags row for whatever's in `tag_search_edit`
        (`TagChipWidget::Kind::ReadOnly`) -- clicking one adds it as an applied filter.
        """
        return chip_names(self.tag_matches_section.raw)

    def search_tags_for(self, query: str) -> None:
        self.tag_search_edit.click()
        self.tag_search_edit.set_text(query)

    def add_tag_filter(self, tag_name: str) -> None:
        """Searches for `tag_name` and clicks its read-only matching chip to AND-combine it into
        the applied filter set (`TabViewModel::addMediaViewerTagFilter`).
        """
        self.search_tags_for(tag_name)
        wait_until(lambda: tag_name in self.tag_matches())
        find_chip(self.tag_matches_section.raw, tag_name).click()

    def remove_tag_filter(self, tag_name: str) -> None:
        find_chip(self.applied_filters_section.raw, tag_name).click_remove()

    def set_name_filter(self, text: str) -> None:
        self.name_edit.click()
        self.name_edit.set_text(text)

    def is_loading(self) -> bool:
        return self.loading_label.exists(timeout=0.3)

    def exit(self) -> None:
        """Clicks "Exit viewer mode" -- same effect as re-toggling the View menu's "Media Viewer
        Mode" action (`TabViewModel::exitMediaViewer`).
        """
        self.exit_button.click()


class RightPanel:
    """The right SidePanelWidget itself. `.tags`/`.preview`/`.monitoring` each make sure the matching
    switch button is checked (so that stack page is actually the visible/interactable one) and
    then return that page's element group -- so e.g.
    `app.right_panel.monitoring.start_monitoring.click()` switches to Monitoring *and* presses
    the button, in one chained call.

    The switch buttons are plain checkboxes, not an auto-exclusive group -- clicking one that's
    already checked toggles it (and its page) *off* rather than being a no-op. So each property
    only clicks when the button isn't already checked, which also makes repeated access to the
    same panel (`app.right_panel.tags` twice in a row) safe/idempotent.
    """

    def __init__(self, window_container: Callable[[], object]):
        self._panel = Element(window_container, AutomationID.RIGHT_PANEL)
        # Public (mirrors LeftPanel's tree_button/search_button) so a test can read a button's
        # checked state -- i.e. whether that tab is the active/expanded one -- without the
        # click-if-unchecked side effect that the tags/preview/monitoring properties below have.
        self.tags_button = Element(window_container, AutomationID.RIGHT_PANEL_TAGS_BUTTON)
        self.preview_button = Element(window_container, AutomationID.RIGHT_PANEL_PREVIEW_BUTTON)
        self.monitoring_button = Element(window_container, AutomationID.RIGHT_PANEL_MONITORING_BUTTON)
        # Hidden except while Media Viewer mode is active on the focused pane's current tab (see
        # MediaViewerSearchPanel's docstring) -- not included in is_expanded()/_select() below,
        # since it's never a tab a test clicks into directly.
        self.searcher_button = Element(window_container, AutomationID.RIGHT_PANEL_SEARCHER_BUTTON)
        self._tags = TagPanel(self._container)
        self._preview = PreviewPanel(self._container)
        self._monitoring = MonitoringPanel(self._container)
        self._searcher = MediaViewerSearchPanel(self._container)

    def _container(self) -> Any:
        return self._panel.raw

    def is_expanded(self) -> bool:
        """Whether the panel is currently expanded, regardless of which tab (Tags/Preview/
        Monitoring) is the active one.
        """
        return self.tags_button.is_checked() or self.preview_button.is_checked() or self.monitoring_button.is_checked()

    @staticmethod
    def _select(button: Element) -> None:
        if not button.is_checked():
            button.click()

    @property
    def tags(self) -> TagPanel:
        self._select(self.tags_button)
        return self._tags

    @property
    def preview(self) -> PreviewPanel:
        self._select(self.preview_button)
        return self._preview

    @property
    def monitoring(self) -> MonitoringPanel:
        self._select(self.monitoring_button)
        return self._monitoring

    @property
    def searcher(self) -> MediaViewerSearchPanel:
        """The Searcher tab's content -- does not click `searcher_button` (see its docstring):
        Media Viewer mode activates this tab itself the moment it's entered.
        """
        return self._searcher
