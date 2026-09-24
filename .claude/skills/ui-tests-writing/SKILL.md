---
name: ui-tests-writing
description: Structural conventions for the ui_tests/ pywinauto black-box suite -- every test suite is a class inheriting from an ExpLorerTestCase-family base class, never a bare module-level test function, with setup/teardown expressed through class hooks rather than duplicated per-test arrange code. Use when writing a new ui_tests/tests/*.py file, adding test methods to an existing one, or deciding whether a new TestCase base class belongs in framework/testcase.py. For general Python style see [[python-best-practices]].
---

# Writing ui_tests Suites

Structural rules for `ui_tests/tests/*.py`. These are conventions for *how tests are organized*,
not what to assert -- for Python style, naming, and typing, see [[python-best-practices]]; for
what makes a good test case, see [[testing-standards]].

## When to Use

- Writing a new file under `ui_tests/tests/`
- Adding test methods to an existing `ui_tests/tests/*.py` file
- Deciding whether a test needs a shared app instance per class or a fresh one per test method
- A test needs setup/teardown behavior that no existing class in `framework/testcase.py` provides

## When NOT to Use

- Non-UI tests under `tests/` (the C++ GTest suites) -- this skill is `ui_tests/`-specific
- Editing `framework/` page-object internals (`app.py`, `pane.py`, etc.) with no new test suite involved

## Core Rules

1. **Every test suite is a class -- no module-level `def test_...()` functions.** A bare function
   can't share setup/teardown with its siblings and invites copy-pasted arrange code across a file.
   Group related test methods into a class even if, today, there's only one.
2. **The class inherits from `ExpLorerTestCase` or another TestCase base class** defined in
   `framework/testcase.py` (exported via `framework/__init__.py`). Never hand-roll app
   startup/teardown inline in a test file -- that logic belongs in a reusable base class.
3. **If no existing TestCase base class fits, add a new one to `framework/testcase.py`** -- don't
   work around the gap with one-off fixtures or manual `ExpLorerApp.start()`/`.close()` calls
   inside the test file itself. The new class becomes available to every future suite with the
   same need, exactly like `ExpLorerTestCase` already is.
4. **Prefer overriding the class's setup/teardown hooks over repeating arrange logic per test
   method.** Use whichever hook actually fits what the setup needs (see "Which hook, when" below)
   rather than duplicating the same three lines at the top of every `test_*` method.
5. **One file may hold multiple TestCase classes.** Splitting is by coverage area, not a
   one-class-per-file rule -- e.g. several small classes in one file, each covering a different
   facet of the same feature, is fine and often clearer than forcing unrelated setup into one class.

## Which Hook, When

`framework/testcase.py` classes are plain pytest xunit-style classes (no `unittest.TestCase`
needed -- see `ExpLorerTestCase`'s own docstring). Pick the hook based on whether it needs a
pytest fixture:

| Hook | Runs | Can receive fixtures (e.g. `tmp_path`)? | Use for |
| --- | --- | --- | --- |
| `setup_class` / `teardown_class` | once per class | No -- classmethods, no fixture args | One-time expensive setup shared by every method (e.g. `ExpLorerTestCase` starting one shared app instance) |
| `setup_method` / `teardown_method` | before/after each test method | No -- only `(self, method)` | Cheap per-method reset that needs no fixture (e.g. clicking the UI back to a known layout) |
| `@pytest.fixture(autouse=True)` method on the class | before/after each test method | Yes | Per-method setup that *does* need a fixture (e.g. a fresh app per test against `tmp_path`, since `tmp_path` can't reach `setup_method`) |

If a new base class needs a fresh app per test method (the shape `test_navigation.py`/
`test_tabs.py`-style bare functions used via the `app` fixture), it must use the autouse-fixture
form, not `setup_method`, because starting the app needs `tmp_path`:

```python
# framework/testcase.py
class ExpLorerFreshAppTestCase:
    app: ExpLorerApp

    @pytest.fixture(autouse=True)
    def _fresh_app(self, tmp_path: Path) -> Iterator[None]:
        ExpLorerApp.kill_stray_instances()
        with ExpLorerApp.start(data_dir=tmp_path / "data") as app:
            self.app = app
            yield
```

Export any new class from `framework/__init__.py` (add it to `__all__`) the same way
`ExpLorerTestCase` already is.

## Examples

```python
# GOOD -- class-based, shared instance across the class, setup_method resets shared state
class TestLifecycleAndConfigDemo(ExpLorerTestCase):
    @classmethod
    def configure_workspace(cls, config: ExpLorerConfig) -> None:
        config.workspace.layout = SplitLayout.TWO_VERTICAL

    def setup_method(self) -> None:
        self.app.layout.two_vertical.click()  # put shared instance back in known state

    def test_starts_in_two_vertical_layout_from_config(self) -> None:
        assert self.app.panes.a.file_browser.exists()
```

```python
# BAD -- bare module-level function, no class, no shared setup/teardown story
def test_new_tab_opens_at_this_pc(app: ExpLorerApp) -> None:
    assert app.panes.a.tabs.titles() == ["This PC"]
```

```python
# BAD -- inline app lifecycle management duplicated in the test file instead of a TestCase class
def test_something():
    ExpLorerApp.kill_stray_instances()
    app = ExpLorerApp.start(data_dir=...)
    try:
        ...
    finally:
        app.close()
```

## Choosing Shared vs. Fresh Instance

- **Shared instance across the class** (`ExpLorerTestCase`): cheaper (amortizes the ~1s app-launch
  cost), but every test method inherits whatever state the previous one left -- use only when
  `setup_method` can reliably reset the relevant state, or when each test method navigates to its
  own isolated `tmp_path` target and never depends on exact counts/indices that a leftover tab,
  history entry, or sort click would perturb.
- **Fresh instance per test method**: required when a test asserts on exact state that must start
  from a known-empty baseline -- tab counts/indices, back/forward enabled/disabled transitions,
  sort-criterion defaults, etc. This is the `ExpLorerFreshAppTestCase`-shape base class described
  above.

When in doubt, look at what the test asserts: if a leftover artifact from another test method in
the same class could make the assertion wrong or flaky, it needs a fresh instance.
