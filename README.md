# Exp-LORer

A Windows-first (Linux later) desktop file explorer built with C++20 and Qt6, featuring a
SQLite-backed file tagging system and a built-in image/video viewer. It aims to go beyond a
traditional file manager by combining fast, Unicode-safe file navigation with database-backed
tagging for organization and an integrated media preview, so you don't need to shell out to
external apps to browse and view your files.

Full requirements and design live in [`Specification.md`](Specification.md) and
[`Architecture.md`](Architecture.md) at the repo root.

> **Status:** under active development. The features below are currently supported; the rest of
> `Specification.md` is still being implemented feature-by-feature.

## Currently Supported Features

- **Multi-tab browsing** — each pane owns its own tabs, with independent navigation history,
  current path, view mode, and sort order.
- **Split-screen layouts** — single pane, two-pane vertical/horizontal split, or a four-pane grid.
- **Tag support** — tags are stored in a local SQLite database and never modify the tagged files
  themselves.
- **Content preview** — folder contents, file/text content, images, and video, plus syntax
  highlighting for a number of common programming languages.
- **Integrated terminal** — a collapsible terminal panel per tab.
- **Quick select by name** — live-selects items whose name matches what you type, without needing
  to press Enter.
- **Color customization** — configurable, ordered rules that apply custom colors/styles to files
  and folders based on name and extension patterns.
- **Fast navigation** — in details view, can use arrows keys to quickly move between items.

## Architecture

Strict Clean Architecture, dependencies point inward only. `src/domain` and `src/application`
never depend on Qt, SQLite, or FFmpeg; those are injected through Port interfaces implemented in
`src/adapters`. See [`Architecture.md`](Architecture.md) for the full layering, threading model,
DB schema, and file-hashing strategy.

- `src/domain` — plain C++ entities (`FileNode`, `Tag`, `MediaMetadata`, `FileTagAssociation`).
- `src/application` — use cases (`FileNavigationUseCase`, `TagManagementUseCase`,
  `MediaProcessingUseCase`) and Port interfaces.
- `src/adapters` — Qt ViewModels, SQLite persistence, filesystem, and media decoding adapters.
- `src/ui` — the Qt Widgets shell and the QML media viewer.
- `src/app` — `CompositionRoot` (wires concrete adapters into use cases) and `main.cpp`.

## Building

### Prerequisites

- Windows, with either a Visual Studio Developer PowerShell or MSVC discoverable via `vswhere`.
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment variable set to
  your local checkout. Dependencies (Qt6 widgets+sql, qtdeclarative, qtmultimedia, sqlite3,
  ffmpeg, spdlog, gtest, xxhash, and others) are declared in `vcpkg.json` and resolved
  automatically by the vcpkg toolchain at configure time.

Before building, make sure the application isn't currently running (kill it first if it is).

> **Note:** some vcpkg packages fail to build with long paths, and generated build paths can get
> very long. To work around this, `CMakePresets.json` sets vcpkg's buildtrees root to `C:/bt` via
> `VCPKG_INSTALL_OPTIONS` (`--x-buildtrees-root=C:/bt`) in every preset. Change that path if
> `C:/bt` doesn't suit your machine.

### Build

```powershell
.\build.ps1                    # configure (if needed) + build the Release preset
.\build.ps1 -Reconfigure       # force a fresh CMake configure
.\build.ps1 -Target Exp-LORer  # build a specific target
```

`build.ps1` auto-imports the MSVC environment via `vswhere`/`vcvarsall.bat` if `cl.exe` isn't
already on `PATH`. It always builds the `Release` preset into `build/Release`.

To use the `windows` preset (Debug, triplet `x64-windows`) directly instead of `build.ps1`:

```powershell
cmake --preset windows
cmake --build build/windows
```

Presets are defined in `CMakePresets.json`: `windows` (Debug) and `Release`
(triplet `x64-windows-release`).

## Testing

Tests use GoogleTest, registered with CTest via `gtest_discover_tests`, mirroring the `src/`
layer structure (`tests/domain`, `tests/application`, `tests/adapters`).

```powershell
cmake --build build/Release --target explorer_domain_tests
ctest --test-dir build/Release
ctest --test-dir build/Release -R explorer_domain_tests   # run one suite
```

Domain tests are plain unit tests; application-layer use cases are tested against mocked Ports
(GoogleMock) with no Qt/SQLite/disk access; adapter tests use a real in-memory (`:memory:`)
SQLite DB and `std::filesystem` temp directories. UI is not covered by CTest in v1.

### UI blackbox tests (opt-in)

A separate Python/pywinauto suite under `ui_tests/` drives the compiled `.exe` through Windows UI
Automation. It's outside the CMake/CTest graph and never runs as part of `build.ps1`. Requires
Python 3 on `PATH` and the app already built.

```powershell
.\ui_tests\setup_env.ps1      # one-time: creates ui_tests\.venv, installs requirements.txt
.\ui_tests\run_ui_tests.ps1   # runs the suite (creates the venv first if missing)
```
