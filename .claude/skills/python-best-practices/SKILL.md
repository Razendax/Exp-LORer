---
name: python-best-practices
description: Comprehensive coding standards for modern Python (3.10+) -- style, type hints, error handling, testing with pytest, and project/tooling conventions. Use when writing, reviewing, or refactoring any Python code in this repo, including the ui_tests/ pywinauto black-box suite and any future scripts/tooling -- not just when the user says "Python" explicitly. For C++ use [[cpp-core-guideline]]; for language-agnostic conventions use [[coding-standards]].
---

## When to Use

* Writing new Python code: scripts, automation, application code, or `ui_tests/` test cases and framework helpers
* Reviewing or refactoring existing Python code
* Adding a dependency, a test fixture, or a new module under `ui_tests/`
* Deciding between idioms (e.g. dataclass vs plain class, `Enum` vs string constants, exceptions vs return codes)

## When NOT to Use

* Non-Python code in this repo (C++ under `src/`/`tests/`) -- use [[cpp-core-guideline]]
* Generic cross-language questions with no Python-specific angle -- use [[coding-standards]]
* Trivial one-off shell one-liners that aren't checked into the repo

## Cross-Cutting Principles

* **Explicit is better than implicit** (PEP 20): no `import *`, no relying on dynamic attribute
  magic when a plain attribute/method would do
* **Readability counts** (PEP 20): optimize for the next reader, not for cleverness or brevity
* **There should be one obvious way to do it**: prefer the standard-library/idiomatic tool over a
  hand-rolled alternative (`pathlib` over string-joined paths, `enum.Enum` over magic strings)
* **Fail fast, fail loud**: don't let errors pass silently unless explicitly silenced -- an empty
  `except:` is almost always a bug
* **Type hints are part of the interface**: they're the fastest way for a reader (or Claude) to
  understand a function's contract without reading its body


## Style & Naming (PEP 8)

### Key Rules

| Rule | Summary |
| --- | --- |
| **Naming** | `snake_case` for functions/variables/modules, `PascalCase` for classes, `UPPER_SNAKE` for module-level constants, leading `_` for internals |
| **Line length** | Keep lines reasonable (formatter-enforced, see Tooling); don't hand-wrap to fight the formatter |
| **Imports** | Grouped stdlib / third-party / local, each group alphabetized, no wildcard imports |
| **f-strings** | Prefer f-strings over `%`-formatting or `.format()` for interpolation |
| **Booleans** | Name predicates as questions: `is_ready`, `has_focus`, not `ready_flag` |
| **Blank class body** | Use `...` or a docstring, never a bare `pass` when a docstring can do both jobs |

```python
# GOOD
from pathlib import Path

import pytest

from framework.app import ExpLorerApp

MAX_RETRIES = 3


def is_config_stale(config_path: Path, max_age_s: float) -> bool:
    ...


# BAD
import *
from framework.app import ExpLorerApp
import pytest
from pathlib import Path

maxRetries = 3  # wrong case for a constant

def checkConfig(p, a):  # wrong case, unclear names, no types
    ...
```


## Type Hints (PEP 484 / 604)

### Key Rules

| Rule | Summary |
| --- | --- |
| **Public functions/methods** | Always annotate parameters and return type |
| **`from __future__ import annotations`** | Put at the top of every module so annotations don't pay runtime cost and forward references just work |
| **Modern union syntax** | Prefer `str \| None` over `Optional[str]`, `int \| float` over `Union[int, float]` (3.10+) |
| **Builtin generics** | Prefer `list[str]`, `dict[str, int]` over `typing.List`/`typing.Dict` |
| **Narrow types beat `Any`** | Use `Protocol`, `TypedDict`, or a small `Enum`/dataclass instead of `dict`/`Any` when the shape is known |
| **Don't over-annotate locals** | Skip type hints on obvious local variables (`count = 0`) -- annotate signatures and non-obvious cases |

```python
from __future__ import annotations

from pathlib import Path


def wait_until(predicate: Callable[[], T], timeout: float = 5.0) -> T: ...

def find_config(data_dir: Path) -> Path | None:
    candidate = data_dir / "config.json"
    return candidate if candidate.exists() else None


# BAD -- no future import, old-style Optional/List, untyped signature
from typing import Optional, List

def find_configs(data_dir):
    results: Optional[List] = None
    ...
```


## Functions & Structure

### Key Rules

| Rule | Summary |
| --- | --- |
| **Single responsibility** | One function does one job describable without "and" |
| **Guard clauses** | Prefer early `return`/`raise` over deep `if`/`else` nesting |
| **Keyword-only for clarity** | For functions with several optional/boolean params, force keyword args with a bare `*` |
| **Avoid mutable default arguments** | Never `def f(items=[])` -- use `None` and create the default inside |
| **Small, composable helpers** | Prefer several small functions over one long one mixing abstraction levels |

```python
# GOOD -- guard clause, keyword-only options, no mutable default
def add_tab(self, path: str = THIS_PC, *, view_mode: str = "Details") -> "PaneConfig":
    if not path:
        raise ValueError("path must not be empty")
    self.tabs.append(TabConfig(path, view_mode))
    return self


# BAD -- mutable default, positional boolean soup
def add_tab(self, path="this-pc:", view_mode="Details", ascending=True, active=False):
    ...
```


## Data Structures & Idioms

### Key Rules

| Rule | Summary |
| --- | --- |
| **`pathlib.Path`** | Always, never manual string path-joining (`os.path.join`/`+`) |
| **`dataclasses`** | Prefer `@dataclass` over a hand-written `__init__` that just assigns fields, unless you need fluent chaining/custom construction logic like the existing config builders |
| **`enum.Enum`** | Prefer over raw string/int constants for a closed set of values; subclass `str, Enum` when the value must also serialize as plain JSON/string |
| **Comprehensions** | Prefer a list/dict/set comprehension or generator expression over a manual `for` loop that only appends/collects |
| **Context managers** | Any resource with cleanup (files, subprocess handles, locks) goes through `with`, never manual open/close |
| **`is` for `None`/singletons** | `if x is None`, never `if x == None` |

```python
# GOOD
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


class SplitLayout(str, Enum):
    SINGLE = "Single"
    TWO_VERTICAL = "TwoVertical"


@dataclass
class TabConfig:
    path: str = "this-pc:"
    view_mode: str = "Details"
    sort_ascending: bool = True


active_paths = [tab.path for tab in tabs if tab.view_mode == "Details"]

with (data_dir / "config.json").open("w", encoding="utf-8") as f:
    f.write(payload)


# BAD
import os

config_path = os.path.join(str(data_dir), "config.json")  # use pathlib instead

active_paths = []
for tab in tabs:
    if tab.view_mode == "Details":
        active_paths.append(tab.path)  # use a comprehension

f = open(config_path, "w")  # no context manager, leaks on exception
f.write(payload)
f.close()
```

### When *not* to reach for `@dataclass`

This repo's `ui_tests/framework/config.py` builders (`PaneConfig`, `WorkspaceConfig`, ...) are
plain classes on purpose: they need fluent chaining (`add_tab(...).add_tab(...)`), a `to_json()`
method, and construction wired to an owner. A `@dataclass` is the right default for a simple bag
of fields; don't force it where custom construction/chaining logic is the point.


## Error Handling

### Key Rules

| Rule | Summary |
| --- | --- |
| **EAFP over LBYL** | Prefer `try`/`except` over pre-checking when the check-then-act is itself racy (e.g. file existence then open) |
| **Narrow `except`** | Catch the specific exception type(s) you can actually handle; never a bare `except:` |
| **Broad catch needs a reason** | If you must catch broadly (e.g. polling a flaky UI predicate), say why in a comment and consider `# noqa` with the specific rule code |
| **Custom exceptions** | Define a small exception class when callers need to distinguish failure modes, rather than parsing a message string |
| **`raise ... from`** | Preserve the original exception as the cause when translating/re-raising, so tracebacks stay useful |
| **No silent swallowing** | An empty `except: pass` is almost always a bug -- log, re-raise, or handle explicitly |

```python
# GOOD -- specific exception, cause preserved
try:
    config = json.loads(path.read_text(encoding="utf-8"))
except (OSError, json.JSONDecodeError) as exc:
    raise RuntimeError(f"failed to load config from {path}") from exc

# GOOD -- deliberately broad, reason documented (mirrors ui_tests/framework/wait.py)
try:
    result = predicate()
except Exception as exc:  # noqa: BLE001 -- predicate may transiently raise mid-rebuild
    last_error = exc


# BAD -- swallows everything silently
try:
    config = json.loads(path.read_text())
except:
    pass
```


## Testing (pytest)

This repo's Python is currently the `ui_tests/` black-box suite -- these apply there and to any
future pytest-based suite.

### Key Rules

| Rule | Summary |
| --- | --- |
| **Arrange/Act/Assert** | Structure each test body in that order, even without explicit comments |
| **Descriptive test names** | `test_<scenario>_<expected_outcome>`, not `test_1` or `test_config` |
| **Fixtures over setup boilerplate** | Use `conftest.py` fixtures for shared setup/teardown instead of copy-pasted per-test init |
| **`tmp_path`/isolated dirs** | Never write test artifacts into a shared/real path; use pytest's `tmp_path` or an equivalent per-test data dir |
| **`pytest.mark.parametrize`** | Prefer parametrization over near-duplicate test functions that only vary an input |
| **One behavior per test** | A test that asserts many unrelated things makes failures hard to diagnose -- split it |
| **`wait_until`, not `time.sleep`** | For async UI state, poll with a helper like `framework/wait.py`'s `wait_until`, never a fixed `time.sleep()` guess |

```python
# GOOD
import pytest


@pytest.mark.parametrize(
    ("layout", "expected_pane_count"),
    [
        (SplitLayout.SINGLE, 1),
        (SplitLayout.TWO_VERTICAL, 2),
        (SplitLayout.FOUR_GRID, 4),
    ],
)
def test_layout_reports_expected_pane_count(layout, expected_pane_count):
    config = ExpLorerConfig(tmp_path)
    config.workspace.layout = layout

    pane_count = count_visible_panes(config)

    assert pane_count == expected_pane_count


# BAD -- vague name, sleep instead of polling, unrelated asserts bundled together
def test_stuff():
    time.sleep(3)
    app = ExpLorerApp.start(data_dir=Path("C:/tmp/shared"))  # shared, non-isolated path
    assert app.window.exists()
    assert app.tabs[0].path == "this-pc:"
    assert app.status_bar.text != ""
```


## Tooling & Project Layout

| Rule | Summary |
| --- | --- |
| **Pin what's declared** | Keep `ui_tests/requirements.txt` minimal and versioned (`>=` floors, as already done) -- don't add a dependency without a reason |
| **Formatter/linter** | If the project adopts one (e.g. `ruff format` + `ruff check`), run it before committing rather than hand-formatting; don't fight a configured formatter's output |
| **`if __name__ == "__main__":`** | Any module runnable as a script guards its entry point this way, even a small helper script |
| **No prints in library code** | Use `logging` (or pytest's own reporting) for library/framework code (`ui_tests/framework/`); reserve `print` for actual CLI/script entry points |
| **Virtual environments** | Never install packages into a global interpreter for project work -- use the project's `.venv` (see `ui_tests/setup_env.ps1`) |


## Scripting & Automation Specifics

Relevant to `ui_tests/` (pywinauto-driven) and any future automation script:

| Rule | Summary |
| --- | --- |
| **`subprocess.run(..., check=True)`** | Prefer over `os.system`/`Popen` when you just need to run a command and check its result |
| **No shell string concatenation** | Pass argument lists to `subprocess`, never build a shell command via string formatting (injection risk, breaks on paths with spaces/non-ASCII) |
| **Timeouts on waits** | Any polling/wait helper takes an explicit timeout and raises a clear error on expiry (see `wait_until`) -- an unbounded wait can hang CI forever |
| **Paths may be non-ASCII** | This app must support non-ASCII file/folder names (see root `CLAUDE.md`) -- always use `pathlib.Path` end-to-end in test code that touches real paths, never assume ASCII-only strings |

```python
# GOOD
import subprocess

subprocess.run(["git", "status", "--short"], check=True, cwd=repo_root)

# BAD -- shell injection risk, breaks on spaces/non-ASCII paths
subprocess.run(f"git status --short {repo_root}", shell=True)
```


## Quick Reference Checklist

Before marking Python work complete, ensure the following:

* `from __future__ import annotations` at the top of new modules
* Public functions/methods are fully type-hinted, using `X | None` / builtin generics
* No mutable default arguments
* `pathlib.Path` used for all filesystem paths, never manual string joins
* No bare `except:`; broad catches are justified with a comment
* No silently swallowed exceptions
* New/changed behavior has a test with a descriptive `test_<scenario>_<outcome>` name
* Tests use fixtures/`tmp_path` for isolation, not shared paths or fixed `time.sleep()`
* No `import *`, imports grouped and alphabetized
* No `print()` in framework/library code -- use `logging` or let pytest report
* Dependencies added to `requirements.txt` only when actually needed
