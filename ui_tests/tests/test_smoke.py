from __future__ import annotations

from framework.app import ExpLorerApp


def test_main_window_appears(app: ExpLorerApp) -> None:
    assert app.exists()
    assert app.is_visible()
    assert app.title == "Exp-LORer"
