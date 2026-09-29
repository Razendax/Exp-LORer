"""Test framework for driving Exp-LORer's UI in `ui_tests/`.

Typical use (see `conftest.py`'s `app` fixture for the common case):

    app = ExpLorerApp.start(data_dir=some_dir)
    try:
        app.navigation.back.click()
        app.right_panel.monitoring.start_monitoring.click()
    finally:
        app.close()

To boot a fresh instance straight into a specific workspace layout instead of driving the UI
into that state by hand, write a config.json with `ExpLorerConfig` before starting it:

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
from .details_view import (
    TOGGLEABLE_COLUMN_TITLES,
    DetailsRow,
    drag_header,
    find_header,
    find_row,
    read_details_rows,
    read_header_titles,
    select_row,
    toggle_column_visibility,
)
from .element import Element
from .favorites_menu import FavoritesMenu
from .manage_favorites_dialog import ManageFavoritesDialog
from .modal_dialog import connect_by_handle, find_control
from .tag_chip import TagChip, chip_names, find_chip, find_chip_label
from .tag_manager_dialog import TagManagerDialog, open_tag_manager_dialog
from .testcase import ExpLorerFreshAppTestCase, ExpLorerTestCase
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
    "find_row",
    "select_row",
    "read_header_titles",
    "find_header",
    "drag_header",
    "toggle_column_visibility",
    "TOGGLEABLE_COLUMN_TITLES",
    "BottomPanel",
    "FolderSizePanel",
    "connect_by_handle",
    "find_control",
    "TagChip",
    "find_chip",
    "find_chip_label",
    "chip_names",
    "TagManagerDialog",
    "open_tag_manager_dialog",
    "FavoritesMenu",
    "ManageFavoritesDialog",
    "ExpLorerTestCase",
    "ExpLorerFreshAppTestCase",
    "wait_until",
]
