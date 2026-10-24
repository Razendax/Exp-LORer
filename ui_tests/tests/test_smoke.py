from __future__ import annotations

from framework.app import WINDOW_TITLE, ExpLorerApp


def test_main_window_appears(app: ExpLorerApp) -> None:
    assert app.exists()
    assert app.is_visible()
    # "Exp-LORer — <workspace name>" since the workspaces feature shipped (see WINDOW_TITLE_RE's
    # own docstring in framework/app.py) -- "Default" for the self-healed default workspace every
    # fresh data_dir boots into, but match by prefix rather than hard-coding that name.
    assert app.title.startswith(WINDOW_TITLE)
