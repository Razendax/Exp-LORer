"""Page object for the top-level "View" menu's checkable entries that have no backing
widget/automation id of their own -- `m_showHiddenFilesAction` (`MainWindow.cpp`,
Architecture.md §14.22) is a plain `QAction`, not a widget, so it gets no `setObjectName()`
(see `automation_id.py`'s module docstring: "QAction-only menu entries with no persistent backing
widget have no id either").

Like `favorites_menu.py`/`tag_manager_dialog.py`'s `open_tag_manager_dialog`, menu items are
clicked through directly by title rather than via pywinauto's `menu_select` -- see those
docstrings for why (`menu_select` fails against this app's QAction-backed QMenu items with a COM
E_UNEXPECTED).
"""

from __future__ import annotations

from typing import Any


class ViewMenu:
    def __init__(self, app: Any):
        self._app = app

    def toggle_show_hidden_files(self) -> None:
        """Opens View and clicks "Show hidden files/folders", flipping the app-wide toggle
        (`FileListModel::kShowHiddenFilesSettingsKey`, persisted via `QSettings`) -- broadcast
        live to every open tab's file lists/folder tree, no re-navigation needed.
        """
        self._app.window.child_window(title="View", control_type="MenuItem").click_input()
        self._app.window.child_window(title="Show hidden files/folders", control_type="MenuItem").click_input()

    def toggle_media_viewer_mode(self) -> None:
        """Opens View and clicks "Media Viewer Mode", flipping the focused tab's `TabViewModel::
        enterMediaViewer()`/`exitMediaViewer()` (Architecture.md §14.61) -- the only entry point
        into/out of the mode besides the Searcher panel's own "Exit viewer mode" button
        (`RightPanel.searcher.exit_button`).
        """
        self._app.window.child_window(title="View", control_type="MenuItem").click_input()
        self._app.window.child_window(title="Media Viewer Mode", control_type="MenuItem").click_input()
