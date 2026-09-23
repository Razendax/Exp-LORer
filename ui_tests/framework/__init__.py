"""Test framework for driving Exp-LORer's UI in `ui_tests/`.

Typical use (see `conftest.py`'s `app` fixture for the common case):

    app = ExpLorerApp.start(data_dir=some_dir)
    try:
        app.navigation.back.click()
        app.right_panel.monitoring.start_monitoring.click()
    finally:
        app.close()

`ExpLorerApp` is the entry point; everything else here is either a page-object group it exposes
(`WorkspacePane`, `RightPanel`, ...) or a low-level building block (`Element`, `find`,
`AutomationID`) for extending the framework to widgets it doesn't cover yet.
"""

from .app import ExpLorerApp
from .automation_id import AutomationID, find
from .element import Element

__all__ = ["ExpLorerApp", "AutomationID", "find", "Element"]
