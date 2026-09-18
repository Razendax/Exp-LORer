import os
import subprocess
import time
from pathlib import Path

import pytest
from pywinauto import Application

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO_ROOT / "build" / "Release" / "src" / "app" / "Exp-LORer.exe"
WINDOW_TITLE = "Exp-LORer"


@pytest.fixture(scope="session")
def exe_path():
    override = os.environ.get("EXP_LORER_EXE")
    path = Path(override) if override else DEFAULT_EXE
    if not path.is_file():
        pytest.fail(
            f"Exp-LORer executable not found at {path}. "
            r"Build the app first: .\build.ps1 "
            "(or set EXP_LORER_EXE to point at a built exe)."
        )
    return path


def _kill_stray_instances():
    subprocess.run(
        ["taskkill", "/IM", "Exp-LORer.exe", "/F"],
        capture_output=True,
    )
    time.sleep(0.5)


@pytest.fixture
def app(exe_path):
    _kill_stray_instances()

    # wait_for_idle=False: WaitForInputIdle can raise ERROR_NOT_GUI_PROCESS (1471) if called before
    # the Qt process has finished initializing its message queue; we wait for the window instead.
    application = Application(backend="uia").start(str(exe_path), wait_for_idle=False)
    try:
        window = application.window(title=WINDOW_TITLE)
        window.wait("visible", timeout=20)
        yield window
    finally:
        try:
            if application.is_process_running():
                window = application.window(title=WINDOW_TITLE)
                window.close()
        except Exception:
            pass
        try:
            if application.is_process_running():
                application.kill()
        except Exception:
            pass
