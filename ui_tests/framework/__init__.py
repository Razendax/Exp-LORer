"""Test framework for driving Exp-LORer's UI in `ui_tests/`.

Typical use (see `conftest.py`'s `app` fixture for the common case):

    app = ExpLorerApp.start(data_dir=some_dir)
    try:
        app.navigation.back.click()
        app.right_panel.monitoring.start_monitoring.click()
    finally:
        app.close()

To boot a fresh instance straight into a specific workspace layout instead of driving the UI
into that state by hand, write its config file with `ExpLorerConfig` before starting it:

    config = ExpLorerConfig(data_dir)
    config.workspace.layout = SplitLayout.TWO_VERTICAL
    config.workspace.panes.a.add_tab()  # defaults to THIS_PC
    config.workspace.save()

    app = ExpLorerApp.start(data_dir=data_dir)

`ExpLorerApp` is the entry point; everything else here is either a page-object group it exposes
(`WorkspacePane`, `RightPanel`, ...), the `ExpLorerConfig` config-file builder, `ExpLorerTestCase`
(a base class for tests that want one Exp-LORer instance shared across a whole class -- see its
own docstring), or a low-level building block (`Element`, `find`, `AutomationID`) for extending
the framework to widgets it doesn't cover yet.
"""

from .app import ExpLorerApp
from .automation_id import AutomationID, find
from .bottom_panel import BottomPanel, FolderSizePanel
from .config import THIS_PC, ExpLorerConfig, SplitLayout, WorkspacePaneId
from .context_menu import ContextMenu, open_context_menu
from .details_view import (
    TOGGLEABLE_COLUMN_TITLES,
    DetailsRow,
    activate_selection_with_enter,
    begin_rename,
    cancel_rename,
    commit_rename,
    confirm_delete_dialog,
    copy_selection,
    cut_selection,
    delete_selection,
    double_click_row,
    drag_header,
    find_header,
    find_row,
    paste_clipboard,
    read_details_rows,
    read_grid_item_names,
    read_header_titles,
    read_name_column,
    rename,
    row_item,
    select_all,
    select_row,
    selected_names,
    toggle_column_visibility,
)
from .element import Element
from .favorites_menu import FavoritesMenu
from .left_panel import FolderTree, LeftPanel
from .manage_favorites_dialog import ManageFavoritesDialog
from .modal_dialog import (
    connect_by_handle,
    find_control,
    visible_top_level_window_handles,
    wait_for_new_top_level_window,
    wait_for_window_handle,
)
from .notification_overlay import NotificationOverlay
from .pane import drag_tab_to_other_pane, drag_tab_to_reorder
from .settings_dialog import SettingsDialog, open_settings_dialog
from .settings_hotkeys_page import SettingsHotkeysPage
from .tag_chip import TagChip, chip_count_text, chip_names, find_chip, find_chip_label
from .tag_manager_dialog import TagManagerDialog, open_tag_manager_dialog
from .testcase import (
    ExpLorerConfiguredFreshAppTestCase,
    ExpLorerFreshAppTestCase,
    ExpLorerPreparedDataDirTestCase,
    ExpLorerRestartableTestCase,
    ExpLorerTestCase,
)
from .wait import wait_until

__all__ = [
    "ExpLorerApp",
    "AutomationID",
    "find",
    "Element",
    "ExpLorerConfig",
    "THIS_PC",
    "SplitLayout",
    "WorkspacePaneId",
    "DetailsRow",
    "read_details_rows",
    "read_grid_item_names",
    "find_row",
    "select_row",
    "selected_names",
    "read_header_titles",
    "read_name_column",
    "find_header",
    "drag_header",
    "toggle_column_visibility",
    "TOGGLEABLE_COLUMN_TITLES",
    "BottomPanel",
    "FolderSizePanel",
    "connect_by_handle",
    "find_control",
    "wait_for_window_handle",
    "visible_top_level_window_handles",
    "wait_for_new_top_level_window",
    "row_item",
    "double_click_row",
    "activate_selection_with_enter",
    "select_all",
    "copy_selection",
    "cut_selection",
    "paste_clipboard",
    "delete_selection",
    "confirm_delete_dialog",
    "begin_rename",
    "commit_rename",
    "cancel_rename",
    "rename",
    "ContextMenu",
    "open_context_menu",
    "NotificationOverlay",
    "TagChip",
    "find_chip",
    "find_chip_label",
    "chip_names",
    "chip_count_text",
    "TagManagerDialog",
    "open_tag_manager_dialog",
    "FavoritesMenu",
    "LeftPanel",
    "FolderTree",
    "ManageFavoritesDialog",
    "SettingsDialog",
    "open_settings_dialog",
    "SettingsHotkeysPage",
    "ExpLorerTestCase",
    "ExpLorerFreshAppTestCase",
    "ExpLorerConfiguredFreshAppTestCase",
    "ExpLorerPreparedDataDirTestCase",
    "ExpLorerRestartableTestCase",
    "drag_tab_to_reorder",
    "drag_tab_to_other_pane",
    "wait_until",
]
