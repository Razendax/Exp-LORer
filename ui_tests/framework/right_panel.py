"""Page objects for the right SidePanelWidget's 3-way tab switcher (Tags / Preview / Monitoring)."""

from __future__ import annotations

from typing import Any, Callable

from .automation_id import AutomationID
from .element import Element
from .tag_chip import chip_names
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
        """Chip names under "Tags on selection" -- the resolved tagging target's own tags
        (TagListViewModel::resolveTarget); empty when nothing resolves (multi-select, or This PC
        root with nothing selected).
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
        self._tags_button = Element(window_container, AutomationID.RIGHT_PANEL_TAGS_BUTTON)
        self._preview_button = Element(window_container, AutomationID.RIGHT_PANEL_PREVIEW_BUTTON)
        self._monitoring_button = Element(window_container, AutomationID.RIGHT_PANEL_MONITORING_BUTTON)
        self._tags = TagPanel(self._container)
        self._preview = PreviewPanel(self._container)
        self._monitoring = MonitoringPanel(self._container)

    def _container(self) -> Any:
        return self._panel.raw

    @staticmethod
    def _select(button: Element) -> None:
        if not button.is_checked():
            button.click()

    @property
    def tags(self) -> TagPanel:
        self._select(self._tags_button)
        return self._tags

    @property
    def preview(self) -> PreviewPanel:
        self._select(self._preview_button)
        return self._preview

    @property
    def monitoring(self) -> MonitoringPanel:
        self._select(self._monitoring_button)
        return self._monitoring
