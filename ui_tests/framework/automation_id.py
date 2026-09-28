"""Registry of UI Automation ids for Exp-LORer widgets.

Every id here mirrors a `setObjectName(...)` call made in the C++ UI code (see Architecture.md
§11's "UI element lookup" convention).

IMPORTANT -- Qt's `uia` accessibility bridge does NOT expose objectName() as a bare
AutomationId. It builds each element's automation_id as the FULL dotted path of every ancestor's
objectName() (falling back to the Qt class name for an unnamed ancestor widget), e.g. the pane
A address bar's real automation_id is
`QApplication.mainWindow.mainSplitter.workspaceLayoutWidget.workspacePane_PaneA.paneToolBar.paneAddressBar`,
not just `"paneAddressBar"`. pywinauto's `child_window(auto_id=...)` does an exact-equality
match (confirmed against this repo's pywinauto version, which has no `auto_id_re`), so a plain
`auto_id=AutomationID.PANE_ADDRESS_BAR` lookup will NOT find it. Use `find(container, name)`
below instead, which matches by id suffix:

    pane = find(window, AutomationID.WORKSPACE_PANE_A)
    address_bar = find(pane, AutomationID.PANE_ADDRESS_BAR)

Most tests shouldn't need to call `find` directly -- the `framework` package (see
`framework/app.py`) wraps every id below in a typed `Element`/page-object property, e.g.
`app.navigation.back` or `app.right_panel.monitoring.start_monitoring`. Reach for `find` only
when the framework doesn't yet cover a widget you need.

Scoping follows the widget's actual runtime multiplicity, not a globally unique name per widget:

- App-wide singletons (MainWindow, WorkspaceLayoutWidget, RightPanelWidget, TagPanelWidget,
  PreviewPanelWidget, MonitoringPanelWidget, MainStatusBarWidget, and on-demand-but-never-concurrent
  modal dialogs) each have one fixed id.
- The 4 WorkspacePaneWidget instances (WORKSPACE_PANE_A..D) each have a per-instance container id;
  everything a pane owns directly (toolbar, tab widget, address bar, status bar, nav buttons) keeps
  ONE fixed id shared across all 4 panes -- scope a lookup through the pane container first (see
  the `find` example above).
- Per-tab content (file browser views, search panels, bottom panel, terminal) also keeps fixed,
  reusable ids: only the current QTabWidget/QStackedWidget page is ever part of the visible UI
  Automation tree, so the shared id is never actually ambiguous. Switch to the tab you need first
  (by title/index), then locate content inside it.
- Fully dynamic, data-driven widgets (tag chips, per-row buttons in the Settings decorations table,
  per-language color swatches) have no static id -- locate them via their (stable) parent
  container's id plus text/index matching instead.
- QAction-only menu entries with no persistent backing widget have no id either -- use
  pywinauto's text-based `menu_select(...)` (or click through the menu directly).
"""

from __future__ import annotations

import re
import time
from typing import Any


class AutomationID:
    # --- Main Window (MainWindow.cpp) ---
    MAIN_WINDOW = "mainWindow"
    MAIN_SPLITTER = "mainSplitter"
    LAYOUT_TOOLBAR = "layoutToolBar"
    LAYOUT_SINGLE_BUTTON = "layoutSingleButton"
    LAYOUT_TWO_VERTICAL_BUTTON = "layoutTwoVerticalButton"
    LAYOUT_TWO_HORIZONTAL_BUTTON = "layoutTwoHorizontalButton"
    LAYOUT_FOUR_GRID_BUTTON = "layoutFourGridButton"
    GLOBAL_SEARCH_BAR = "globalSearchBar"
    ADVANCED_SEARCH_BUTTON = "advancedSearchButton"
    WORKSPACE_PANE_A = "workspacePane_PaneA"
    WORKSPACE_PANE_B = "workspacePane_PaneB"
    WORKSPACE_PANE_C = "workspacePane_PaneC"
    WORKSPACE_PANE_D = "workspacePane_PaneD"
    WORKSPACE_LAYOUT_WIDGET = "workspaceLayoutWidget"
    RIGHT_PANEL = "rightPanelWidget"
    RIGHT_PANEL_STACK = "rightPanelStack"
    RIGHT_PANEL_TAGS_BUTTON = "rightPanelTagsButton"
    RIGHT_PANEL_PREVIEW_BUTTON = "rightPanelPreviewButton"
    RIGHT_PANEL_MONITORING_BUTTON = "rightPanelMonitoringButton"
    TAG_PANEL = "tagPanelWidget"
    PREVIEW_PANEL = "previewPanelWidget"
    MONITORING_PANEL = "monitoringPanelWidget"
    MAIN_STATUS_BAR = "mainStatusBarWidget"
    MAIN_STATUS_DRIVE_LABEL = "mainStatusDriveLabel"
    MAIN_STATUS_COMPUTER_NAME_LABEL = "mainStatusComputerNameLabel"

    # --- Workspace Pane (shared across all 4 WORKSPACE_PANE_* instances -- WorkspacePaneWidget.cpp) ---
    PANE_TOOLBAR = "paneToolBar"
    PANE_TAB_WIDGET = "paneTabWidget"
    PANE_BACK_BUTTON = "paneBackButton"
    PANE_FORWARD_BUTTON = "paneForwardButton"
    PANE_UP_BUTTON = "paneUpButton"
    PANE_REFRESH_BUTTON = "paneRefreshButton"
    PANE_VIEW_MODE_BUTTON = "paneViewModeButton"
    PANE_NEW_TAB_BUTTON = "paneNewTabButton"
    PANE_ADDRESS_BAR = "paneAddressBar"
    PANE_ADDRESS_BAR_POPUP = "paneAddressBarPopup"
    PANE_STATUS_BAR = "paneStatusBar"
    PANE_STATUS_COUNTS_LABEL = "paneStatusCountsLabel"
    PANE_STATUS_QUICK_SELECT_EDIT = "paneStatusQuickSelectEdit"

    # --- Per-Tab Content (WorkspacePaneWidget/FileBrowserView/SearchResultsPane/SearchCriteriaPanel/
    #     BottomPanelWidget/TerminalWidget -- fixed ids reused per tab; safe since only the current
    #     tab/stack page is ever part of the visible UI Automation tree) ---
    FILE_BROWSER_VIEW = "fileBrowserView"
    FILE_LIST_VIEW = "fileBrowserListView"
    FILE_TREE_VIEW = "fileBrowserTreeView"
    QUICK_SEARCH_RESULTS_VIEW = "quickSearchResultsView"
    ADVANCED_SEARCH_RESULTS_PANE = "advancedSearchResultsPane"
    SEARCH_CRITERIA_PANEL = "searchCriteriaPanel"
    SEARCH_NAME_EDIT = "searchNameEdit"
    SEARCH_MIN_SIZE_SPIN = "searchMinSizeSpin"
    SEARCH_MAX_SIZE_SPIN = "searchMaxSizeSpin"
    SEARCH_SUBMIT_BUTTON = "searchSubmitButton"
    ADVANCED_SEARCH_CLOSE_BUTTON = "advancedSearchCloseButton"
    SEARCH_TAG_EDIT = "searchTagEdit"
    BOTTOM_PANEL = "bottomPanelWidget"
    # Index-derived (BottomPanelWidget::addPanelTab), in registration order: Terminal is tab 0,
    # Folder Size (Architecture.md §14.39) is tab 1.
    BOTTOM_PANEL_TERMINAL_BUTTON = "bottomPanelTabButton_0"
    BOTTOM_PANEL_FOLDER_SIZE_BUTTON = "bottomPanelTabButton_1"
    TERMINAL_WIDGET = "terminalWidget"
    FOLDER_SIZE_PIE_CHART = "pieChartWidget"
    FOLDER_SIZE_SCAN_FOLDER_BUTTON = "folderSizeScanFolderButton"
    FOLDER_SIZE_SCAN_DRIVE_BUTTON = "folderSizeScanDriveButton"

    # --- Preview Panel (PreviewPanelWidget.cpp, singleton) ---
    PREVIEW_IMAGE_LABEL = "previewImageLabel"
    PREVIEW_TEXT_VIEW = "previewTextView"
    PREVIEW_FOLDER_LIST = "previewFolderList"
    PREVIEW_LOADING_LABEL = "previewLoadingLabel"
    PREVIEW_UNSUPPORTED_LABEL = "previewUnsupportedLabel"
    PREVIEW_ERROR_LABEL = "previewErrorLabel"

    # --- Tag Panel (TagPanelWidget.cpp, singleton) ---
    TAG_PANEL_SEARCH_EDIT = "tagPanelSearchEdit"
    TAG_PANEL_CREATE_TAG_BUTTON = "tagPanelCreateTagButton"
    TAG_PANEL_FOLDER_TAGS_SECTION = "tagPanelFolderTagsSection"
    TAG_PANEL_SEARCH_MATCHES_SECTION = "tagPanelSearchMatchesSection"
    TAG_PANEL_SELECTED_ITEM_TAGS_SECTION = "tagPanelSelectedItemTagsSection"

    # --- Monitoring Panel (MonitoringPanelWidget.cpp, singleton) ---
    MONITORING_START_STOP_BUTTON = "monitoringStartStopButton"
    MONITORING_PIN_BUTTON = "monitoringPinButton"
    MONITORING_LOG_VIEW = "monitoringLogView"
    MONITORING_TOTAL_LABEL = "monitoringTotalLabel"

    # --- Settings Dialog (SettingsDialog.cpp, modal, never more than one open) ---
    SETTINGS_DIALOG = "settingsDialog"
    SETTINGS_CATEGORY_TREE = "settingsCategoryTree"
    SETTINGS_PAGE_STACK = "settingsPageStack"
    SETTINGS_SHOW_TAB_CLOSE_BUTTONS_CHECKBOX = "settingsShowTabCloseButtonsCheckBox"
    SETTINGS_DECORATIONS_TABLE = "settingsDecorationsTable"
    SETTINGS_ADD_DECORATION_RULE_BUTTON = "settingsAddDecorationRuleButton"
    SETTINGS_CLOSE_BUTTON = "settingsCloseButton"

    # --- Tag Manager Dialog (TagManagerDialog.cpp, modal, single instance at a time) ---
    TAG_MANAGER_DIALOG = "tagManagerDialog"
    TAG_MANAGER_SEARCH_EDIT = "tagManagerSearchEdit"
    TAG_MANAGER_RENAME_BUTTON = "tagManagerRenameButton"
    TAG_MANAGER_ADD_BUTTON = "tagManagerAddButton"
    TAG_MANAGER_DELETE_BUTTON = "tagManagerDeleteButton"
    TAG_MANAGER_CLOSE_BUTTON = "tagManagerCloseButton"

    # --- Manage Favorites Dialog (ManageFavoritesDialog.cpp, modal, single instance at a time) ---
    MANAGE_FAVORITES_DIALOG = "manageFavoritesDialog"
    MANAGE_FAVORITES_TREE = "manageFavoritesTree"
    MANAGE_FAVORITES_ADD_FILE_BUTTON = "manageFavoritesAddFileButton"
    MANAGE_FAVORITES_ADD_FOLDER_BUTTON = "manageFavoritesAddFolderButton"
    MANAGE_FAVORITES_ADD_CATEGORY_BUTTON = "manageFavoritesAddCategoryButton"
    MANAGE_FAVORITES_RENAME_BUTTON = "manageFavoritesRenameButton"
    MANAGE_FAVORITES_MOVE_TO_CATEGORY_BUTTON = "manageFavoritesMoveToCategoryButton"
    MANAGE_FAVORITES_MOVE_UP_BUTTON = "manageFavoritesMoveUpButton"
    MANAGE_FAVORITES_MOVE_DOWN_BUTTON = "manageFavoritesMoveDownButton"
    MANAGE_FAVORITES_REMOVE_BUTTON = "manageFavoritesRemoveButton"
    MANAGE_FAVORITES_CLOSE_BUTTON = "manageFavoritesCloseButton"

    # --- File Decoration Rule Dialog (FileDecorationRuleDialog.cpp, modal, single instance at a time) ---
    DECORATION_RULE_DIALOG = "fileDecorationRuleDialog"
    DECORATION_RULE_PATTERNS_EDIT = "decorationRulePatternsEdit"
    DECORATION_RULE_COLOR_BUTTON = "decorationRuleColorButton"
    DECORATION_RULE_RESET_COLOR_BUTTON = "decorationRuleResetColorButton"
    DECORATION_RULE_FONT_FAMILY_EDIT = "decorationRuleFontFamilyEdit"
    DECORATION_RULE_FONT_FAMILY_LIST = "decorationRuleFontFamilyList"
    DECORATION_RULE_FONT_SIZE_LIST = "decorationRuleFontSizeList"
    DECORATION_RULE_BOLD_CHECKBOX = "decorationRuleBoldCheckBox"
    DECORATION_RULE_ITALIC_CHECKBOX = "decorationRuleItalicCheckBox"
    DECORATION_RULE_UNDERLINE_CHECKBOX = "decorationRuleUnderlineCheckBox"
    DECORATION_RULE_STRIKEOUT_CHECKBOX = "decorationRuleStrikeoutCheckBox"
    DECORATION_RULE_PREVIEW_LABEL = "decorationRulePreviewLabel"
    DECORATION_RULE_BUTTON_BOX = "decorationRuleButtonBox"
    DECORATION_RULE_HELP_LABEL = "decorationRuleHelpLabel"


def find(container: Any, name: str, timeout: float = 5) -> Any:
    """Return the descendant of `container` whose UI Automation id ends with `name`.

    Works around Qt's `uia` bridge reporting each element's automation_id as the full dotted
    ancestor path rather than a bare objectName() (see the module docstring) -- pywinauto's own
    `auto_id=` criterion only does exact-string matching, so it can't be used directly for
    anything but the top-level window. `container` is anything with a `.descendants()` method
    (a pywinauto WindowSpecification or wrapper, e.g. the app's main `window` or a container
    already found with this function).

    Raises LookupError if nothing matches within `timeout` seconds.
    """
    pattern = re.compile(r"(^|\.)" + re.escape(name) + r"$")
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            for element in container.descendants():
                automation_id = element.element_info.automation_id
                if automation_id and pattern.search(automation_id):
                    return element
        except Exception as exc:  # pragma: no cover -- transient UIA COM errors during teardown/build
            last_error = exc
        time.sleep(0.2)
    raise LookupError(f"No descendant of {container!r} found with UI Automation id suffix {name!r} ({last_error})")
