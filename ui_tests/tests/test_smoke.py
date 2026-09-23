def test_main_window_appears(app):
    assert app.exists()
    assert app.is_visible()
    assert app.title == "Exp-LORer"
