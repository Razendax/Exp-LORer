from __future__ import annotations

import os
import shutil
import time
from collections.abc import Callable, Iterator
from pathlib import Path

import pytest

from framework import ExpLorerApp

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO_ROOT / "build" / "Release" / "src" / "app" / "Exp-LORer.exe"


@pytest.fixture(scope="session")
def exe_path() -> Path:
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
def app(exe_path: Path, tmp_path: Path) -> Iterator[ExpLorerApp]:
    """A fresh Exp-LORer instance per test, isolated to a private `tmp_path/data` directory.

    Need a specific/shared/pre-populated data directory instead? Use the `start_app` fixture.
    """
    ExpLorerApp.kill_stray_instances()
    with ExpLorerApp.start(data_dir=tmp_path / "data", exe_path=exe_path) as explorer:
        yield explorer


@pytest.fixture
def start_app(exe_path: Path) -> Iterator[Callable[[Path], ExpLorerApp]]:
    """Factory fixture for tests that need to seed a `config.json` (via `ExpLorerConfig`) before
    the app starts, or that call `.close()` mid-test to assert on what got saved -- both need
    more control than the `app` fixture's fixed "start against a blank data_dir" flow gives.

    `start_app(data_dir)` kills stray instances and launches Exp-LORer against `data_dir` (like
    `app` does), but doesn't touch `data_dir` itself -- write a config.json there first if the
    test wants to boot into a specific state. Guarantees `.close()` at teardown even if the test
    never calls it, or raises before it can; calling `.close()` yourself mid-test is fine too,
    it's idempotent (see `ExpLorerApp.close`), so the teardown call becomes a no-op.
    """
    started: list[ExpLorerApp] = []

    def _start(data_dir: Path) -> ExpLorerApp:
        ExpLorerApp.kill_stray_instances()
        instance = ExpLorerApp.start(data_dir=data_dir, exe_path=exe_path)
        started.append(instance)
        return instance

    yield _start

    for instance in started:
        instance.close()


@pytest.fixture(autouse=True)
def _cleanup_tmp_path(tmp_path: Path) -> Iterator[None]:
    """pytest's own `tmp_path` cleanup only prunes directories left over from old *sessions* (it
    keeps the last few around for post-mortem debugging) -- it never deletes a test's own
    directory once that test finishes. Every test here launches an Exp-LORer process that reads,
    writes, and watches inside `tmp_path`, so force a real delete once the test (and its app, via
    `app`/`start_app`'s own teardown, which always runs first) is done.

    Retries briefly: Windows can keep a directory handle open for a short moment after a process
    is killed, before the OS actually releases it.
    """
    yield
    last_error = None
    for _ in range(10):
        try:
            shutil.rmtree(tmp_path)
            return
        except FileNotFoundError:
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.3)
    raise AssertionError(f"Could not delete {tmp_path} after test: {last_error}")
