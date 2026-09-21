# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Exp-LORer is a Windows-first (Linux later) desktop file explorer in C++20/Qt6, with a SQLite-backed
file tagging system and a built-in image/video viewer. The project is in early scaffolding: most
layers under `src/` currently contain only `.gitkeep` placeholders and `tests/` contains only
placeholder GTest files — `CompositionRoot`, `MainWindow`, and `main.cpp` are the only real code so far.

Full requirements and design are in `Specification.md` and `Architecture.md` at the repo root — read
these before implementing a new component or feature, refactoring the code, or making changes in code;
they define the layering, threading model, DB schema, file hashing strategy, and v1 scope exclusions in detail.
Do not restate their content here — refer to them.

## Development Workflow

This project is developed feature-by-feature via a plan/review/implement cycle (except bug fixes which can be done directly).
Follow it automatically, without being asked each time:

**Planning a feature** (when asked to plan a new feature, not yet to implement it):
1. Produce the plan (use plan mode as normal).
2. Save the plan to `.claude/active_work/plan.md` (create the directory if it doesn't exist;
   overwrite any existing `plan.md`). This directory is gitignored — it's local scratch state, not
   committed.
3. In the same turn, update `Architecture.md` and/or `Specification.md` if the feature changes the
   layering, Ports, schema, or requirements they describe — don't wait to be asked. If nothing in
   the feature affects them, leave them untouched.
4. Stop there. The user reviews/edits the plan and doc changes (and may clear context) before
   implementation starts — do not start implementing in the same turn a plan was just written.

**Implementing a feature** (when asked to implement from `.claude/active_work/plan.md`):
1. Follow the plan.
2. When implementation is complete, rename `.claude/active_work/plan.md` to
   `.claude/active_work/prev_plan.md` (overwrite any existing `prev_plan.md`).

## Build

Requires `VCPKG_ROOT` set to a vcpkg checkout, and either a Visual Studio Developer PowerShell or MSVC
discoverable via `vswhere`.

Before building, ensure that the application is not running, if it is, kill it.

```powershell
.\build.ps1                    # configure (if needed) + build the Release preset
.\build.ps1 -Reconfigure        # force a fresh CMake configure
.\build.ps1 -Target Exp-LORer   # build a specific target
```

`build.ps1` auto-imports the MSVC environment via `vswhere`/`vcvarsall.bat` if `cl.exe` isn't already on
PATH. It always builds the `Release` preset into `build/Release`.

Presets are defined in `CMakePresets.json`: `windows` (Debug, triplet `x64-windows`) and `Release`
(triplet `x64-windows-release`). To use the `windows` preset directly instead of `build.ps1`:

```powershell
cmake --preset windows
cmake --build build/windows
```

Dependencies (Qt6 widgets+sql, qtdeclarative, qtmultimedia, sqlite3, ffmpeg, spdlog, gtest, xxhash) are
declared in `vcpkg.json` and resolved automatically by the vcpkg toolchain file at configure time.

Qt's `qwindows.dll` platform plugin is copied into a `platforms/` folder next to the executable as a
post-build step (`src/app/CMakeLists.txt`) since vcpkg's applocal DLL copy doesn't handle it — if you
add a new executable target that links Qt, replicate that custom command or the app will fail to start.

## Tests

Tests use GoogleTest, registered with CTest via `gtest_discover_tests`, mirroring the `src/` layer
structure (`tests/domain`, `tests/application`, `tests/adapters`).

```powershell
cmake --build build/Release --target explorer_domain_tests
ctest --test-dir build/Release
ctest --test-dir build/Release -R explorer_domain_tests   # run one suite
```

Per `Architecture.md` §11: domain tests are plain unit tests with no fixtures; application-layer use
cases are tested against mocked Ports (GoogleMock) and must not touch Qt, SQLite, or disk; adapter
tests use a real in-memory (`:memory:`) SQLite DB and `std::filesystem` temp directories. UI is not
covered by CTest in v1.

### UI blackbox tests (opt-in, not part of the build)

A separate Python/pywinauto suite under `ui_tests/` drives the compiled `.exe` through Windows UI
Automation. It is entirely outside the CMake/CTest graph and never runs as part of `build.ps1` — a
developer runs it by hand. Requires Python 3 on PATH and the app already built (`.\build.ps1`).

```powershell
.\ui_tests\setup_env.ps1      # one-time: creates ui_tests\.venv, installs requirements.txt
.\ui_tests\run_ui_tests.ps1   # runs the suite (creates the venv first if missing)
```

## Architecture

Strict Clean Architecture — dependencies point inward only. `src/domain` and `src/application` must
never `#include` Qt, SQLite, or FFmpeg headers; all such dependencies are injected through Port
interfaces (`IFileSystemRepository`, `ITagRepository`, `IMediaDecoder`) defined in `src/application`
and implemented in `src/adapters`.

- `src/domain` — plain C++ entities (`FileNode`, `Tag`, `MediaMetadata`, `FileTagAssociation`), no framework deps.
- `src/application` — use cases (`FileNavigationUseCase`, `TagManagementUseCase`, `MediaProcessingUseCase`) and the Port interfaces above.
- `src/adapters/viewmodels` — Qt-facing ViewModels wrapping use cases (`FileTreeViewModel`, `TagListViewModel`, `MediaPreviewViewModel`).
- `src/adapters/persistence` — `SQLiteTagRepository` + migrations.
- `src/adapters/filesystem` — `StandardFileSystemRepository` (`std::filesystem`, Windows long-path `\\?\` handling, OS trash calls).
- `src/adapters/media` — `QtMediaDecoder`/`FFmpegMediaDecoder`, thumbnail generation.
- `src/ui/widgets` — Qt Widgets shell (main window, panels, dialogs).
- `src/ui/qml` — QML media viewer, embedded into the Widgets shell via `QQuickWidget`.
- `src/app` — `CompositionRoot` (constructs concrete adapters and injects them into use cases — the only place concrete Qt/SQLite/FFmpeg types meet the use cases) and `main.cpp`.

Key conventions from `Architecture.md` to preserve when adding code:

- **Paths**: carried as `std::filesystem::path` everywhere in domain/application/adapters, never
  `std::string`. The app MUST support non-ASCII (accented/CJK/Cyrillic/emoji) file and folder names —
  this is a hard requirement (Specification.md line 34), not an edge case. **Never call
  `std::filesystem::path::string()`** (or `.generic_string()`) anywhere on a path that can contain
  non-ASCII characters: on Windows it narrows through the process's ANSI code page and **throws
  `std::system_error`** — uncaught, this crashes the whole app.
  Use `PathUtf8::toUtf8()` (`src/domain/PathUtf8.h`) instead for any narrow/UTF-8 text needed for
  comparisons, sorting, error messages, log lines, or the SQLite boundary in `SQLiteTagRepository`.
  Actual file I/O keeps using the `path` object (or `.wstring()` on Windows) directly, never a
  narrowed string. See Architecture.md §6 ("Paths") for the full rationale.
- **Errors**: domain/application layers return `std::expected<T, Error>` (or an equivalent `Result<T>`),
  not exceptions; adapters may catch third-party exceptions at the boundary and translate them.
- **Threading**: no disk/DB I/O on the Qt UI thread — dispatch through the Application layer's Ports to
  background threads/thread pools; marshal results back via queued Qt signals/slots.
- **Naming**: `PascalCase` types, `camelCase` methods/variables, `m_` prefix for private members, `I`
  prefix for Port interfaces.
- **File identity**: files are identified by a fast partial hash (size + first/last 64KB + mtime via
  xxHash64), not a cryptographic hash — this is a heuristic for detecting renames/moves, not a
  uniqueness guarantee.
- **Single instance**: enforced via a named mutex (Windows) / flock'd PID file (Linux), forwarding
  startup args to the running instance over local IPC rather than starting a second process.
