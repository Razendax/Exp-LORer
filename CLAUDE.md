# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Exp-LORer is a Windows-first (Linux later) desktop file explorer in C++20/Qt6, with a SQLite-backed
file tagging system and a built-in image/video viewer. The project is well past initial scaffolding:
`src/` is fully built out across all layers (domain, application, adapters, ui, app) with a growing
feature set (workspaces, tagging, search, git status, duplicates, disk usage, fragmentation, terminal,
media preview, and more — see `docs/features/`), and `tests/` has real GoogleTest coverage mirroring
the `src/` layer structure.

Full requirements and design are in `docs/Specification.md` and `docs/Architecture.md` — read
these before implementing a new component or feature, refactoring the code, or making changes in code;
they define the layering, threading model, DB schema, file hashing strategy, and v1 scope exclusions in detail.
Do not restate their content here — refer to them.

`Architecture.md` §14 is a per-feature decision log that outgrew one file; it's split **by feature
topic** across nine files under `docs/features/` — `01-workspaces-and-panels.md`,
`02-file-operations-and-navigation.md`, `03-search.md`, `04-tagging.md`,
`05-preview-and-decoration.md`, `06-status-and-monitoring.md`, `07-media-viewer.md`,
`08-disk-and-file-tools.md`, `09-app-infrastructure.md`. Section numbers (§14.1–§14.70) were
assigned chronologically as each feature shipped and are kept stable wherever they now live, since
code comments cite them directly — so a given file's own numbers are not necessarily contiguous;
each file's intro lists its exact set. `Architecture.md` itself still holds §1–§13 (the core
architecture) plus a short index into those nine files — read the relevant one(s) for the feature
you're touching, picking by *topic*, not by guessing a section-number range.

When a feature's decision-log entry needs to be added (step 3 below), pick whichever existing
`docs/features/*.md` file its topic fits best and append the new §14.N section there, updating
that file's "Covers §..." intro. If the feature doesn't fit any existing file's topic well, create
a new topically-named file (next free number, e.g. `10-...`) instead of forcing it into a
loosely-related one, and add it to `Architecture.md` §14's index.

## Development Workflow

This project is developed feature-by-feature via a spec/plan/implement cycle (except bug fixes,
which can be done directly). Each stage is its own skill under `.claude/skills/` — invoke it by
name or let it trigger automatically from the request's phrasing; follow the cycle without being
asked each time. All three stages write into `.claude/active_work/` (create it if it doesn't
exist); that directory is gitignored — it's local scratch state, not committed.

1. **`spec-feature`** — turns a feature request into `.claude/active_work/spec.md` (docs-only; no
   source code or graphify).
2. **User reviews** `spec.md`, approving it or editing it directly — no agent action needed until
   the user asks for a plan.
3. **`plan-feature`** — turns the reviewed spec into `.claude/active_work/plan.md`, divided into
   vertical-slice milestones; queries the `graphify-out/` knowledge graph first (see the
   `graphify` skill).
4. **`implement-feature`** — implements the plan (in whole or by milestone), querying graphify as
   needed and tracking milestone progress in `plan.md`.

Do not smoke test — don't launch the app yourself to click through or verify a feature/fix after
implementing it. The user tests manually and will come back with whatever needs fixing. This is
separate from the automated suites in `## Tests`, which remain fine to build/run. UI blackbox
coverage (`ui_tests/`) is added as its own later step — don't write or run it as part of a
feature/fix unless explicitly asked to.

## Build

Requires `VCPKG_ROOT` set to a vcpkg checkout, and either a Visual Studio Developer PowerShell or MSVC
discoverable via `vswhere`. Building the optional Windows installer (`-Package` below) additionally
requires NSIS's `makensis.exe` on PATH.

`build.ps1` kills a running `Exp-LORer.exe` before building, so it doesn't need to be stopped manually first.

```powershell
.\build.ps1                    # configure (if needed) + build the Release preset
.\build.ps1 -Reconfigure        # force a fresh CMake configure
.\build.ps1 -Target Exp-LORer   # build a specific target
.\build.ps1 -Package            # also build the per-user Windows installer (.exe) via CPack/NSIS
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
- `src/adapters/viewmodels` — Qt-facing ViewModels wrapping use cases (`TagListViewModel`, `FilePreviewViewModel`, `WorkspaceController`); one target, folder-grouped by feature (`filesystem/`, `workspace/`, `tags/`, `media/`, `git/`, `monitoring/`, `diskusage/`, `search/`, `duplicates/`, `fragmentation/`) the same way `src/application` and `src/ui/widgets` are — see `Architecture.md` §10.
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
