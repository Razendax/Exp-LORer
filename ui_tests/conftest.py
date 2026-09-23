import os
from pathlib import Path

import pytest

from framework import ExpLorerApp

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO_ROOT / "build" / "Release" / "src" / "app" / "Exp-LORer.exe"


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


@pytest.fixture
def app(exe_path, tmp_path):
    """A fresh Exp-LORer instance per test, isolated to a private `tmp_path/data` directory.

    Need a specific/shared/pre-populated data directory instead? Don't use this fixture -- call
    `ExpLorerApp.start(data_dir=..., exe_path=...)` directly in the test.
    """
    ExpLorerApp.kill_stray_instances()
    with ExpLorerApp.start(data_dir=tmp_path / "data", exe_path=exe_path) as explorer:
        yield explorer
