# Architecture Document

## 1. Architectural Approach

The project follows **Clean Architecture**: source dependencies point inward only. Qt, SQLite, and
FFmpeg are treated as replaceable plugins behind Ports defined in the Application layer — Domain
and Application code never depends on them directly. This keeps the core testable, UI-framework-
independent, and resilient to changes in external libraries.

## 2. Layer Breakdown

Four concentric layers, innermost to outermost.

### 2.1 Domain Layer

Plain C++ entities, no framework dependencies:

* `FileNode` — a file or directory: path, size, dates, type, identity hash (§8), and an optional
  `displayName` override used only by synthetic entries (e.g. "This PC" drives, §14.15).
* `Tag` — id, name, hex color.
* `MediaMetadata` — resolution, duration, codec.
* `FileTagAssociation` — maps a `FileNode`'s hash/path to a `Tag`'s id; path is the primary key,
  hash is the fallback used to relocate a row after a rename/move (§7 "Path resolution").

### 2.2 Application Layer

Use cases and Port interfaces; depends only on Domain. Use cases are stateless — a single shared
instance safely serves concurrent callers from every tab/pane (§14.2).

**Use cases:**
* `FileNavigationUseCase` — directory listing/navigation, sorting, filtering (extension, name, size
  range, combined criteria), file CRUD, opening files with their default app, building/invoking the
  context menu.
* `TagManagementUseCase` — create/update/delete/assign tags; resolves broken paths via file hash;
  aggregates tags for the tag panel and ranks tag search results.
* `MediaProcessingUseCase` — thumbnail/metadata extraction for images and video.
* `FilePreviewUseCase` — classifies a target as folder/text/image/video/unsupported and builds a
  `FilePreview` for the right panel's Preview tab (§14.25): folder listing (directories first),
  capped text-prefix read, or a decoded thumbnail via `IMediaDecoder`. Stateless, depends only on
  `IFileSystemRepository`/`IMediaDecoder` — never on another use case, same posture as the rest of
  this layer.
* `GitStatusUseCase` — thin stateless passthrough to `IGitStatusProvider`, backing the Details-view
  git status column (§14.31).

**Ports:**
* `IFileSystemRepository` — file operations: list, recursive list, move, copy, delete, hash,
  single-path stat, create folder/file, open-with-default-app, show OS properties dialog, read a
  capped byte prefix of a file (`readFilePrefix`, §14.25), extract archive content
  (`extractArchive`, §14.28), and resolve a path to a real, on-disk file for callers that need
  actual file I/O (`materializeForReading` — identity for a real path, a private temp-extracted
  copy for an entry inside an archive, §14.28).
* `ITagRepository` — persists tags and file/tag associations.
* `IMediaDecoder` — decodes media streams, extracts thumbnails.
* `IContextMenuProvider` — builds a registry/COM-sourced context menu as plain data
  (`ContextMenuEntry`) and invokes the chosen entry — it hands back data, it doesn't show the OS's
  own popup (§14.13).
* `IGitStatusProvider` — resolves a directory's working-tree git status (per-immediate-child
  `GitFileStatus`, or an empty result when the directory isn't inside a repository), §14.31.
* `IDirectoryWatcher` — non-recursive immediate-children change notification backing live directory
  update (§14.32).
* `IFolderMonitor` — recursive, subtree-wide change notification with per-event
  add/remove/modify classification, backing per-tab folder monitoring (§14.33). Kept separate from
  `IDirectoryWatcher` (Interface Segregation) since the two serve different callers with different
  contracts: `IDirectoryWatcher` is a cheap "something changed, re-list" nudge for whatever tab is
  currently visible, while `IFolderMonitor` is an explicit, opt-in, full-subtree event log a user
  starts and stops themselves.

### 2.3 Interface Adapters Layer

Converts between Domain/Application types and framework types.

* **ViewModels** (`adapters/viewmodels`) — `FileListModel` (one shared Qt table model feeding both
  list and tree views, §2.3.1), `TabViewModel` (a tab's navigation, view mode, sort, selection, and
  search state), `WorkspacePaneViewModel`/`WorkspaceController` (pane/tab bookkeeping and cross-pane
  mediation, §14), `TagListViewModel` (tag panel, retargeted to the focused tab, §14.9),
  `FileOperationsController` (clipboard/delete/open/context-menu orchestration, §14.10),
  `TagManagerViewModel` (global tag CRUD, §14.17), `MediaPreviewViewModel` (QML-facing state; not
  yet implemented).
* **Persistence** (`adapters/persistence`) — `SQLiteTagRepository`, implements `ITagRepository`
  (§7, §10).
* **Filesystem** (`adapters/filesystem`) — `StandardFileSystemRepository`, implements
  `IFileSystemRepository` via `std::filesystem`; Windows long-path (`\\?\`) handling; UTF-8
  conversion for paths happens only at the SQLite boundary, not here (§6).
* **Shell** (`adapters/shell`) — `ShellContextMenuProvider`, implements `IContextMenuProvider` by
  reading Windows Registry verbs and optional COM shell-extension handlers (§14.13).
* **Config** (`adapters/config`) — `AppConfigStore`, JSON session/window-geometry persistence
  (§14.14).
* **Media** (`adapters/media`) — `MediaDecoder`, the concrete `IMediaDecoder` registered in
  `CompositionRoot`; dispatches by extension to `VipsImageDecoder` (images, via libvips) or
  `FFmpegMediaDecoder` (video, one representative frame via libavformat/libavcodec/libswscale) —
  each self-contained, not cross-calling the other. `ThumbnailCache` (on-disk, keyed by
  path+size+mtime+requested dimensions, §8) and `CachingMediaDecoder` (a decorator implementing
  `IMediaDecoder` around it) sit in front so callers never see cache logic. §14.25.
* **Logging** (`adapters/logging`) — `Logging`, a thin wrapper over spdlog (rotating file sink,
  console sink in debug builds); the only place spdlog is referenced (§6, §14.22).
* **Git** (`adapters/git`) — `LibGit2Repository` (RAII wrapper over libgit2, the only place libgit2
  is referenced), `LibGit2StatusProvider`, implements `IGitStatusProvider` (§14.31).

### 2.3.1 View Modes

Seven Explorer-style view modes (Extra Large/Large/Medium/Small Icons, List, Details, Tiles) share
one `FileListModel`. A `QStackedWidget` (`FileBrowserView`) switches between a `QListView` (icon/
list/tiles) and a `QTreeView` (details, with native column-header sorting). Tiles and the four
icon-size modes use custom `QStyledItemDelegate`s (`FileTileDelegate`, `FileIconDelegate`); List
uses Qt's stock delegate. Icons come from `QFileIconProvider` (real OS shell icons). View mode is
per-tab presentation state, not a Domain/Application concern.

### 2.4 Frameworks & Drivers Layer

* **UI:** Qt 6 Widgets for the main shell; Qt Quick/QML (via `QQuickWidget`) for the image/video
  viewer.
* **Database:** SQLite.
* **Media:** Qt Multimedia and/or FFmpeg.
* **OS:** Windows (primary), Linux (planned).

## 3. Component Relationships & Data Flow

Every operation follows the same inward-then-outward shape: **UI → ViewModel → Use Case → Port →
Adapter**, with results flowing back through Qt signals.

Example — navigating to a directory: a `QTreeView` click calls `TabViewModel::navigateTo(path)` →
`FileNavigationUseCase::listDirectory(path)` → `IFileSystemRepository` (implemented by
`StandardFileSystemRepository`, using `std::filesystem::directory_iterator`) returns `FileNode`s →
the use case hands them back to the ViewModel, which emits a signal → `FileListModel` updates
whichever view is currently shown.

Tagging, media processing, and every other operation follow the same shape, substituting the
relevant use case/Port/adapter (e.g. tagging a file goes through `TagManagementUseCase` →
`ITagRepository` → `SQLiteTagRepository`).

## 4. Main Components Overview

| Layer | Representative components |
| --- | --- |
| Domain | `FileNode`, `Tag`, `MediaMetadata`, `FileTagAssociation` |
| Application | `FileNavigationUseCase`, `TagManagementUseCase`, `MediaProcessingUseCase`, and their Ports (§2.2) |
| Adapters | `SQLiteTagRepository`, `StandardFileSystemRepository`, `ShellContextMenuProvider`, `AppConfigStore`, plus the ViewModels listed in §2.3 |
| UI (Widgets) | `MainWindow`, `WorkspaceLayoutWidget`, `WorkspacePaneWidget`, `FileBrowserView`, `RightPanelWidget`, `TagPanelWidget`, `ContextMenuBuilder`, dialogs |
| UI (QML) | Media viewer (not yet implemented) |

## 5. Concurrency and Threading Model

* **Main (UI) thread:** Qt event loop and ViewModels only — no direct disk/DB I/O.
* **Design intent:** Application-layer Port calls (file scanning, copying, DB queries, media
  decoding) dispatch to background threads; results marshal back to ViewModels via queued Qt
  signals/slots.
* **Current state:** most Port calls (navigation, file ops, context menu, recursive search) are
  still synchronous on the UI thread — an accepted, tracked gap against the design intent above,
  not a regression. Background dispatch is deferred throughout §14, with one exception:
  `FilePreviewViewModel` (§14.25) dispatches `FilePreviewUseCase` calls to a dedicated worker
  thread, since preview generation (image/video decode) is the first Port-adjacent call slow
  enough, and frequent enough, to need it. "Cancelling" an in-flight preview means discarding a
  stale result by a generation-id check when it arrives, not aborting the decode itself — the
  underlying libvips/FFmpeg calls aren't interruptible mid-call.
* **Database concurrency:** `SQLiteTagRepository` runs SQLite in serialized/thread-safe mode (or a
  single dedicated DB thread) to avoid `SQLITE_BUSY` during concurrent tag reads/writes. This
  covers in-process concurrency only — cross-process concurrency is prevented separately by
  single-instance enforcement (§6).

## 6. Technology, Language & Build Decisions

* **C++20**; MSVC v143+ (Windows), GCC 12+/Clang 15+ (Linux).
* **Windows first**; all OS-specific code goes through `IFileSystemRepository` so a Linux backend
  can be added without touching Domain/Application.
* **Qt 6** (Widgets + Quick/QML hybrid, §2.4).
* **vcpkg** manifest mode (`vcpkg.json`) for all third-party dependencies (Qt6, SQLite3, FFmpeg,
  spdlog, GoogleTest/GoogleMock, xxHash, libarchive per §14.28, libgit2 per §14.31), consumed via
  `find_package`; `CMakePresets.json` pins the vcpkg toolchain.
* **Logging:** `spdlog`, wrapped behind a thin `Logging::log(...)` call site so it isn't referenced
  outside one adapter header.
* **Dependency injection:** no framework — a single composition root
  (`src/app/CompositionRoot.cpp`) constructs concrete adapters and injects them via constructor
  injection. Use cases/ViewModels only ever depend on Application-layer interfaces.
* **Paths:** carried as `std::filesystem::path` through Domain/Application/Adapters, never raw
  `std::string`. Windows long paths (>260 chars) go through the `\\?\` prefix, applied via the
  shared `WindowsLongPath::withPrefix()` helper (`src/domain/WindowsLongPath.h`) by every adapter
  that opens a path directly (`StandardFileSystemRepository`, `VipsImageDecoder`,
  `FFmpegMediaDecoder`, `CachingMediaDecoder`) rather than each reimplementing it; conversion to a
  narrow `std::string` happens at two kinds of
  boundary, both routed through `PathUtf8::toUtf8()` (`src/domain/PathUtf8.h`) rather than
  `std::filesystem::path::string()` — on Windows the latter narrows through the process's ANSI code
  page and throws for filenames it can't represent (accented/CJK/Cyrillic/emoji), which
  `u8string()`-based `PathUtf8::toUtf8()` cannot: at the SQLite boundary (`SQLiteTagRepository`,
  since SQLite stores/compares `TEXT` as UTF-8) and wherever Domain/Application/Adapters need narrow
  text from a path for comparisons, error messages, or log lines (e.g.
  `FileNavigationUseCase`'s name/extension filters, `StandardFileSystemRepository`'s error messages).
  Actual file I/O keeps using the `path` object (or `.wstring()` on Windows) directly and is
  unaffected by this.
* **Single-instance enforcement:** a named mutex (`CreateMutexW`, Windows) / `flock`'d PID file
  (Linux) per user session. A second launch forwards its startup args to the running instance over
  local IPC (e.g. `QLocalSocket`) and exits. Exists to stop two processes writing to `explorer.db`/
  the thumbnail cache concurrently (§5, §8). **Not yet implemented.** When it is, the mutex name/PID
  file path must be derived from the resolved app-data directory (§9), not a fixed name — otherwise
  two instances started against different `--data-dir` overrides (e.g. parallel `ui_tests/` runs, or
  a test run alongside the developer's real running instance) would incorrectly collide with each
  other despite writing to entirely separate data directories.
* **vcpkg overlay ports:** a repo-local `vcpkg-overlays/ports/` directory, registered via a root
  `vcpkg-configuration.json`, holds small custom ports for third-party pieces the public vcpkg
  registry doesn't package (e.g. individual tree-sitter grammars, §14.26) — each is a normal vcpkg
  port (`vcpkg.json` + `portfile.cmake`) that fetches a pinned upstream release archive and builds
  it, so acquisition still goes through vcpkg rather than vendoring generated sources into the repo
  or introducing a second fetch mechanism (e.g. CMake `FetchContent`).

## 7. Database Schema (SQLite)

Single SQLite file at the app-data location (§9), migrated via `PRAGMA user_version`. Current
schema:

```sql
CREATE TABLE Tags (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    hex_color   TEXT NOT NULL,
    created_at  INTEGER NOT NULL
);

CREATE TABLE Files (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL UNIQUE,
    file_hash     TEXT NOT NULL,        -- see §8 hashing strategy
    size          INTEGER NOT NULL,
    modified_at   INTEGER NOT NULL,
    last_seen_at  INTEGER NOT NULL
);
CREATE INDEX idx_files_hash ON Files(file_hash);

CREATE TABLE FileTags (
    file_id  INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
    tag_id   INTEGER NOT NULL REFERENCES Tags(id) ON DELETE CASCADE,
    PRIMARY KEY (file_id, tag_id)
);
```

* **Path resolution:** `TagManagementUseCase` looks up by `path` first; if missing/changed, falls
  back to `file_hash` (+ `size`) to relocate the row and update `path`.
* **Bulk tagging:** wrapped in a single `BEGIN IMMEDIATE ... COMMIT` transaction.
* **Multi-tag AND filtering:** `GROUP BY file_id HAVING COUNT(DISTINCT tag_id) = <N>`.

## 8. File Identity, Hashing & Thumbnail Caching

* **File identity hash:** a fast partial hash — `size + first 64KB + last 64KB + mtime`, mixed via
  xxHash64 — not cryptographic, since files can be large media. Sufficient to detect renames/moves;
  a documented heuristic, not a uniqueness guarantee (two files with identical size/edges/mtime
  collide — an accepted trade-off for tagging, not a security boundary).
* **Thumbnail cache:** on disk under the app-local cache directory (`ThumbnailCache`,
  `adapters/media`, §14.25), entries keyed by `xxHash64(path, size, modified_at, requested width,
  requested height)` rather than the file-identity hash above — a changed source file naturally
  produces a different cache filename, so no separate staleness bookkeeping is needed; the
  identity hash above is a rename/move-detection heuristic for tagging (§7), a different problem,
  and isn't involved here. An in-memory LRU sits in front for the visible viewport. Shared by the
  Preview tab's image/video thumbnails (§14.25) and, later, grid-view icons.
* **Cache eviction:** capped by total size (default 512MB); least-recently-accessed entries pruned
  on startup or when exceeded.

## 9. Application Data Locations & Configuration

* **Locations:** database, logs, and thumbnail cache live under `%LOCALAPPDATA%/Exp-LORer/`
  (Windows), via `QStandardPaths` so the Linux path (`~/.local/share`, `~/.cache`) needs no code
  change. Logs specifically live in a `logs/` subfolder there (§14.22), written via
  `Logging` (`adapters/logging`).
* **`--data-dir <path>` startup override:** `main.cpp` resolves the effective app-data directory
  once, before constructing `Logging` or `CompositionRoot`, checking for a `--data-dir` argument
  before falling back to `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)`.
  `CompositionRoot`'s constructor takes the resolved directory and derives `explorer.db`,
  `config.json`, `cache/thumbnails/`, `highlight_theme.json`, `file_decorations.json`, and
  `favorites.json` from it, instead of each looking it up independently. Exists so `ui_tests/` (§11)
  can point each test run
  at an isolated, disposable directory rather than the developer's real session. On Windows the
  argument is read from the process's own wide command line (`GetCommandLineW()` +
  `CommandLineToArgvW()`), not the narrow `argv` `main()` receives — the same ANSI-code-page
  narrowing trap `PathUtf8`/`path::string()` guidance (§6) warns about applies here too, since the
  override value is itself a filesystem path.
* **Session/settings:** window geometry and the split-window session (layout, each pane's tabs —
  path/view mode/sort order — focused pane) persist to `config.json` (§14.14), written by
  `AppConfigStore`, same directory as `explorer.db`. `QSettings` remains in use only for the
  context-menu shell-extensions toggle (§14.13), not yet migrated; the thumbnail cache size limit is
  unimplemented in either mechanism. Favorites persist separately to their own `favorites.json`
  (§14.35), same directory, same tolerant-load/atomic-save shape as `highlight_theme.json`/
  `file_decorations.json`.
* **Recycle Bin / Trash:** "Delete" routes through `IFileSystemRepository` to the native OS trash
  (`SHFileOperation`/`FOF_ALLOWUNDO` on Windows; deferred to the freedesktop.org spec on Linux). A
  separate "Delete Permanently" action bypasses trash. Both go through the same Port so the
  Application layer is unaware of the OS-specific mechanism.

## 10. Project Directory Structure

```
Exp-LORer/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── src/
│   ├── domain/          # Entities — no Qt/SQLite/FFmpeg deps
│   ├── application/     # Use cases + Port interfaces
│   ├── adapters/
│   │   ├── viewmodels/  # ViewModels/controllers (Qt-facing; §2.3, §14)
│   │   ├── persistence/ # SQLiteTagRepository + migrations
│   │   ├── filesystem/  # StandardFileSystemRepository
│   │   ├── shell/       # ShellContextMenuProvider
│   │   ├── config/      # AppConfigStore (JSON session persistence)
│   │   ├── favorites/   # FavoriteItem / FavoriteCategory / Favorites (§14.35)
│   │   ├── media/       # QtMediaDecoder / FFmpegMediaDecoder (not yet implemented)
│   │   ├── archive/     # ArchivePathResolver / LibArchiveReader / ArchiveIndexCache (§14.28)
│   │   ├── git/         # LibGit2Repository / LibGit2StatusProvider (§14.31)
│   │   └── logging/     # Logging (spdlog wrapper: rotating file + console sinks, §14.22)
│   ├── ui/
│   │   ├── widgets/     # Qt Widgets shell: MainWindow, panes, dialogs, delegates
│   │   └── qml/         # QML media viewer (not yet implemented)
│   └── app/
│       ├── CompositionRoot.cpp/.h
│       ├── ExpLorerApplication.cpp/.h   # QApplication subclass catching event-loop exceptions (§14.22)
│       ├── ExceptionHandler.cpp/.h      # fatal-exception dialog + logging + exit (§14.22)
│       └── main.cpp
├── tests/
│   ├── domain/          # plain unit tests
│   ├── application/     # use case tests against mocked Ports (GoogleMock)
│   └── adapters/        # SQLite (:memory:) and filesystem (temp dir) tests
└── resources/           # icons, qml.qrc, default tag color palette
```

## 11. Testing Strategy

* **Framework:** GoogleTest + GoogleMock via CTest (`gtest_discover_tests`).
* **Domain:** plain unit tests, no fixtures.
* **Application:** use cases tested against mocked Ports — must not touch Qt, SQLite, or disk. This
  is the primary regression-safety net.
* **Adapters:** `SQLiteTagRepository` against a real in-memory (`:memory:`) SQLite DB (same
  migrations as production); `StandardFileSystemRepository` against `std::filesystem` temp
  directories.
* **UI:** out of scope for automated CTest coverage in v1; smoke-tested manually. A separate,
  opt-in blackbox smoke suite lives under top-level `ui_tests/` (Python + pywinauto, driving the
  compiled `.exe` through Windows UI Automation) — deliberately outside the CTest graph, with no
  CMake target and no involvement in `build.ps1`. It only proves the app launches and its main
  window appears; it is not a substitute for the CTest-based coverage above. Each test launches the
  `.exe` with `--data-dir` (§9) pointed at a fresh per-test temp directory, so runs are isolated from
  the developer's real `%LOCALAPPDATA%` session and from each other.
* **UI element lookup — `objectName()` as UI Automation `AutomationId`:** widgets `ui_tests/` needs
  to find get a stable `setObjectName("...")` call right after construction. Confirmed live against
  a built `.exe`: Qt's `uia` accessibility bridge does **not** expose a bare `objectName()` as the
  element's `AutomationId` — it reports the full dotted path of every ancestor's `objectName()`
  (falling back to the Qt class name for an unnamed ancestor), e.g. the pane A address bar's real
  id is `QApplication.mainWindow.mainSplitter.workspaceLayoutWidget.workspacePane_PaneA.paneToolBar.paneAddressBar`,
  not just `"paneAddressBar"`. pywinauto's `child_window(auto_id=...)` only does exact-string
  matching, so tests must use `ui_tests/AutomationID.py`'s `find(container, name)` helper (suffix
  match over `container.descendants()`) instead of passing an `AutomationID` constant straight to
  `auto_id=`. `AutomationID` is still the single registry of `UPPER_SNAKE_CASE` Python names → those
  id strings — tests import names (and `find`) from it rather than hardcoding raw id strings or
  full ancestor paths. Scoping follows the widget's actual runtime multiplicity rather than giving
  every widget a globally-unique name:
  * **App-wide singletons** (`MainWindow`, `WorkspaceLayoutWidget`, `RightPanelWidget`,
    `TagPanelWidget`, `PreviewPanelWidget`, `MonitoringPanelWidget`, `MainStatusBarWidget`, and
    on-demand-but-never-concurrent modal dialogs) get one fixed id.
  * **The 4 `WorkspacePaneWidget` instances** (PaneA–PaneD, §14.1/§14.2 — all 4 constructed up
    front and never destroyed, up to 4 simultaneously visible in `FourGrid`) each get a
    per-instance id on the pane container itself, derived from `WorkspacePaneViewModel::id()`;
    everything a pane owns directly (toolbar, tab widget, address bar, status bar, nav buttons)
    keeps ONE fixed id shared across all 4 panes — tests disambiguate by scoping the lookup through
    the pane container first, not by suffixing every inner widget.
  * **Per-tab content** (file browser views, search panels, bottom panel, terminal — unbounded, N
    tabs × 4 panes) also keeps fixed, reusable ids: `QTabWidget`/`QStackedWidget` only ever expose
    the *current* page, so a hidden tab's or hidden stack page's subtree isn't part of the visible
    UI Automation tree and the shared id is never actually ambiguous. Tests switch to the specific
    tab they need first (by title/index), then locate content inside it.
  * **Fully dynamic, data-driven widgets** (tag chips, per-row buttons in dynamically rebuilt
    tables/lists) are out of scope for static ids — locate them via their (now-stable) parent
    container's id plus text/index matching instead.
  * **QAction-only menu entries with no persistent backing widget** are out of scope — use
    pywinauto's text-based `menu_select(...)`; only actions realized as a persistent toolbar
    `QToolButton` (e.g. the Layout buttons, Advanced Search) get an id, via
    `toolBar->widgetForAction(action)->setObjectName(...)`.
* **Carve-out — OS-shell side effects with no assertable return** (`ShellExecuteW`, the registry/COM
  context-menu building and invocation, live drive/known-folder enumeration): not covered by
  automated adapter tests, since registry contents and installed shell extensions are live,
  per-machine state and invoking an entry launches a real external process with nothing meaningful
  to assert — these are manually smoke-tested instead. The use-case-layer passthrough in front of
  each is still covered by an ordinary mock-based test. This carve-out is referenced throughout §14
  for each feature it applies to.
* **CI expectation:** every use case and repository implementation needs a corresponding test
  before merging; `ctest` must pass.

## 12. Coding Standards

* **Naming:** `PascalCase` types, `camelCase` methods/variables, `m_` prefix for private members,
  `I` prefix for Port interfaces.
* **Headers:** `#pragma once`; one class per header/source pair; Domain/Application headers must
  not `#include` Qt, SQLite, or FFmpeg.
* **Error handling:** Domain/Application layers return `std::expected<T, Error>` (or an equivalent
  `Result<T>`) rather than throwing; adapters may catch third-party exceptions at the boundary and
  translate them.
* **Formatting:** `.clang-format` (LLVM style, 4-space indent), enforced via pre-commit/CI.

(See CLAUDE.md for the same conventions in quick-reference form.)

## 13. Explicit v1 Scope Exclusions (Assumptions)

* Undo/redo for file operations and tag assignment.
* Linux-specific trash integration (falls back to permanent delete until implemented).
* Automated UI testing (a minimal opt-in blackbox smoke check exists under `ui_tests/`, §11 — not
  part of the v1 CTest suite; comprehensive automated UI coverage remains excluded).
* Internationalization (English-only; `tr()` wrapping still used so it isn't precluded later).

## 14. Split-Window Extension (Fixed Layouts, Per-Pane Tabs) and Later Features

Everything in this section extends the UI/Adapters layers only — **no Domain or Application layer
redesign** — and is recorded here as a decision log, not an implementation walkthrough; see git
history and the code's own comments (many cite these subsection numbers directly) for the detail.

### 14.1 Scope and Product Decisions

Splits own tabs, not the reverse: a fixed `SplitLayout` (`Single`, `TwoVertical`, `TwoHorizontal`,
`FourGrid` — exactly these four, no arbitrary nesting) produces up to 4 fixed pane slots, each
independently owning its own tabs and navigation toolbar. Panes keep their tab state even when
hidden by a layout showing fewer panes. The tag panel and media preview are single shared instances
retargeted to whichever pane/tab has focus, mediated by `WorkspaceController`.

### 14.2 Key Architectural Decisions

* Domain/Application layers are unchanged — panes/tabs are pure UI-session state.
* `FileNavigationUseCase` stays stateless; one shared instance serves every pane/tab.
* Exactly one `MainWindow`; no pane/tab component ever opens a second top-level window.
* `WorkspaceController` owns exactly 4 `WorkspacePaneViewModel`s (flat, fixed, no composite tree);
  the active `SplitLayout` only controls which are visible.

### 14.3 New Components by Layer

`src/domain`/`src/application` — unchanged. Key new `adapters/viewmodels` types: `SplitLayout`,
`WorkspacePaneId` (plain enums), `WorkspaceLayoutTopology::visiblePanes(SplitLayout)` (pure,
unit-tested), `TabViewModel` (owns one `NavigationHistory` + `ViewMode` + `FileListModel` + current
path — a tab's full state; Command-pattern navigation history), `WorkspacePaneViewModel` (owns an
ordered `TabViewModel` list for one pane), `WorkspaceController` (the Mediator — owns the 4 panes,
current layout, and focused pane; the hook point for retargeting shared ViewModels). New
`src/ui/widgets` types: `WorkspaceLayoutWidget` (builds nested `QSplitter`s per layout, hiding
rather than destroying panes on a layout switch), `WorkspacePaneWidget` (one pane's toolbar +
tabbed `FileBrowserView`s).

### 14.4 Component Relationship

```
MainWindow
 └── WorkspaceController (owns SplitLayout + focused pane)
      ├── WorkspacePaneViewModel × 4 ── TabViewModel × N ── FileListModel
      ├── TagListViewModel (shared) ◄── retargeted to focused pane's active tab
      └── MediaPreviewViewModel (shared) ◄── retargeted likewise
                                                │
                                                ▼
                                  FileNavigationUseCase (shared, stateless)
                                                │
                                                ▼
                                  IFileSystemRepository (shared)
```

### 14.5 SOLID / Pattern Rationale

SRP separates a tab's state (`TabViewModel`), a pane's tab bookkeeping (`WorkspacePaneViewModel`),
and cross-pane mediation (`WorkspaceController`) into three types with three distinct reasons to
change. New layouts extend `SplitLayout`/`WorkspaceLayoutTopology` without touching the ViewModels
(OCP). Ports stay pane-unaware (ISP). Panes never reference each other directly, only emit signals
(DIP). Patterns: Factory Method (`CompositionRoot`), Mediator (`WorkspaceController`), Observer (Qt
signals/slots), Command (`NavigationHistory`).

### 14.6 Usage Rules

`FileNavigationUseCase` stays stateless; cross-pane coordination goes only through
`WorkspaceController`; ViewModels are created only via `CompositionRoot`'s factories (or
`WorkspacePaneViewModel::addTab()` for a session-time new tab) — UI code never constructs them
directly; `SplitLayout`/`WorkspacePaneId`/`WorkspaceLayoutTopology` stay plain C++, with the
`QSplitter` tree built from them separately; exactly one `MainWindow`; no `WorkspacePaneId`/
`SplitLayout` may appear in `src/domain`/`src/application`; no pane/tab component creates its own
thread; `NavigationHistory` does not itself re-validate paths or do I/O — callers do that.

### 14.7 Navigation History and Threading

History is per-tab (one `NavigationHistory` per `TabViewModel`, discarded when the tab closes; no
global/shared back-forward). Threading is unchanged from §5: concurrent tab navigations are
independent tasks on the same shared repository/pool, results marshaled back only to the
originating tab's model.

### 14.8 Deferred / Future Work

Live/continuous session autosave (save-on-close is implemented, §14.14); cross-pane drag-and-drop.
An empty pane revealed by a layout switch may auto-seed a tab at the focused pane's path, or start
with just a "+" button — either is acceptable.

### 14.9 Tag Panel and Selection Tracking

The shared `TagListViewModel` is owned by `WorkspaceController` and retargeted (`setActiveTab`) on
every focus/tab change, refreshing three sections: the focused tab's folder-plus-ancestor tags,
live tag search results, and the selected item's tags. Selection is per-tab state
(`TabViewModel::selectedEntries()`, a `std::vector<FileNode>` since §14.16), cleared on every
navigation. The tagging target resolves to: the single selected entry; the current folder itself
(via `IFileSystemRepository::stat`) if nothing is selected; or no target if more than one item is
selected (multi-item tagging isn't built). `SQLiteTagRepository` is the first concrete
`ITagRepository` implementation, introduced for this feature. Kept synchronous on the UI thread
(§5). Tag chips render via a `FlowLayout` (wrapping row layout) and `TagChipWidget`
(read-only/addable/removable/selectable pill styles).

### 14.10 Keyboard Hotkeys and Clipboard File Operations

`FileBrowserView` installs a key-press event filter on just its two child views (not a
window-scoped shortcut), so Ctrl+C/X/V, Delete (trash), Shift+Delete (permanent), and Backspace
(parent folder) are only active while the file list has focus — text fields like the address bar
are unaffected. Letter-jump type-ahead comes for free from Qt's `keyboardSearch()`.
`FileOperationsController` (shared, owned by `WorkspaceController`) writes the real OS clipboard
(`QMimeData::setUrls()` + a `DROPEFFECT` custom format) so cut/copy/paste interoperate with File
Explorer; `pasteInto` auto-renames on collision and clears the clipboard after a cut-paste. After
any operation it emits `directoryContentsMayHaveChanged(path)`, which `WorkspaceController` uses to
refresh every tab (any pane, visible or not) browsing that path — covering a paste that spans two
panes. Since §14.16, these act on the tab's full multi-item selection, not a single item.

### 14.11 Opening Files with the Default Application

Double-click/Enter (Qt's built-in `activated` signal) opens a file via the new
`IFileSystemRepository::openWithDefaultApplication` (`ShellExecuteW` on Windows), routed through the
Port like every other file operation rather than a widget calling `QDesktopServices` directly.
Synchronous on the UI thread, consistent with the other file operations. Not covered by an
automated adapter test (§11's OS-shell carve-out); the use-case passthrough is.

### 14.12 Sort Order (Per-Tab, All View Modes)

Sort state (`SortCriterion` + ascending flag) lives on `FileListModel`, already owned 1:1 by
`TabViewModel` and already shared by every view mode, so sorting it once covers all seven modes and
survives navigation for free. Both the `QTreeView` header click and a View-menu "Sort by" submenu
funnel into the same `FileListModel::setSortCriterion`. The submenu is retargeted to the focused
tab via `WorkspaceController::focusedTabChanged`, the same pattern §14.9 established. New tabs start
at the default (Name, ascending). Persisting the choice across restarts is covered by §14.8/§14.14.

### 14.13 Custom Context Menu (Registry-Sourced, Files/Folders/Background Differ)

Right-click shows a custom, Qt-drawn `QMenu` — not the native shell popup — but its *content* still
comes from the Windows Registry (and, optionally, COM shell-extension `IContextMenu` handlers), so
per-file-type and third-party entries (7-Zip, Git, ...) still appear. Files, folder rows, and empty
background each consult different registry roots (`HKCR\<ext>\shell`, `HKCR\Directory\shell`,
`HKCR\Directory\Background\shell`, respectively, plus shared roots), so the three targets can show
genuinely different items. Two user-selectable resolution modes exist: static verbs only (fast) and
static + shell extensions (closer Explorer parity, COM activation cost).

* **14.13.1 Registry roots** — as above; `HKEY_CLASSES_ROOT` already merges HKLM/HKCU, so no
  separate pass is needed.
* **14.13.2 Port and entities** — `IContextMenuProvider` builds a `Result<vector<ContextMenuEntry>>`
  for an item selection or the background, then `invoke(entryId, ownerWindow)` runs the chosen one
  and `discardMenu()` releases any COM handlers kept alive. `ContextMenuEntry`/`ContextMenuIcon` are
  plain data types crossing the Application boundary, the same posture as `std::filesystem::path`.
  Since §14.16 a multi-item selection can be passed; single-target actions are disabled in that case
  (§14.13.6).
* **14.13.3 `ShellContextMenuProvider`** — implements the Port on Windows only (a `Result` failure
  elsewhere). Enumerates `shell\<verb>` registry subkeys for labels/icons/commands, suppressing verb
  names that already have app-native equivalents (open/cut/copy/paste/delete/rename/properties) so
  each action isn't wired twice. Builds "Open with" and "New" submenus from their own registry
  conventions. In shell-extension mode, additionally `CoCreateInstance`s each registered
  `IContextMenu` handler and merges its entries in, keeping the interface alive until the next build
  or `discardMenu()` since `invoke()` must call back into the specific handler that produced a given
  id.
* **14.13.4 New native actions** — the app now implements Rename (a same-directory `moveFile`
  behind an inline, in-place edit box drawn over the item itself — F2, the context menu's
  "Rename", or right after New Folder all route through it; no modal dialog), New
  Folder/New-from-template, and Properties (`IFileSystemRepository::showProperties`,
  `SHObjectProperties` on Windows) itself, rather than relying on the old native popup for them.
* **14.13.5 UI wiring** — `FileOperationsController` builds the entry list (no `QMenu`/`QAction`
  construction itself); `ContextMenuBuilder` (`src/ui/widgets`) turns that data plus the fixed
  native actions into an actual `QMenu`, interleaved at Explorer-conventional positions. A View-menu
  toggle (persisted via `QSettings`) selects the resolution mode. A registry/COM-invoked entry
  always triggers a directory refresh afterward, since the app can't know what it changed on disk.
* **14.13.6 Threading and not built here** — synchronous on the UI thread (§5, same as
  `ShellExecuteW`). Not built: a Linux `IContextMenuProvider`; a drag-and-drop-triggered menu;
  `ShellNew`'s binary-`Data` mechanism; inline rename in the advanced-search-results view (its rows
  use `SearchResultDelegate` and can span multiple directories). Rename/Properties/Open stay
  single-target and are disabled for a multi-selection, since their backing APIs are single-path;
  Cut/Copy/Delete-equivalent entries do act on the whole selection.

### 14.14 Session Persistence (Window Geometry, Layout, Tabs, View & Sort)

The split-window session (layout, each pane's tabs — path/view mode/sort — focused pane, window
geometry) persists to a JSON file (`config.json`) rather than `QSettings`, so it can hold
structured per-pane/per-tab state.

* `WorkspaceConfig`/`TabConfig`/`PaneConfig` (`adapters/viewmodels/WorkspaceConfig.h`) are plain C++
  snapshots; `WorkspaceController::captureConfig()`/`restoreFromConfig()` convert to/from live
  pane/tab state.
* `AppConfig`/`AppConfigStore` (`adapters/config/`, new module) are the Qt/JSON persistence
  boundary — the one place this data is allowed to touch `QByteArray`/`QJsonDocument`. Window
  geometry round-trips through Qt's own `saveGeometry()`/`restoreGeometry()` rather than a
  hand-rolled struct. Enum values serialize as readable strings, not raw ints. `load()` is tolerant
  of a missing file or any malformed field, falling back to defaults field-by-field; `save()` writes
  to a temp file and renames over the target so a crash mid-write can't corrupt the previous file.
* **Save-on-close only**, from `MainWindow::closeEvent()` — no periodic/live autosave in this
  increment, including the one shared set of Details-view column widths (also captured only at
  close, from whichever tab/pane is currently focused).
* **Not built here:** live autosave; per-folder column widths (this app keeps one shared set
  app-wide); migrating the `QSettings`-backed shell-extensions toggle (§14.13) into `config.json`; a
  config schema-migration framework beyond a reserved `"version"` field; bookmarks/pinned folders;
  thumbnail cache size limit.

### 14.15 "This PC" Virtual Navigation Location

A synthetic top-level location — all local/remote drives plus quick-access folders (Downloads,
Documents, Pictures, Videos, Music, Desktop) — reached from any drive's root via Up. Modeled as a
**sentinel path** (`VirtualPaths::ThisPC`, literal `L"this-pc:"` — not a legal path component, so no
real file can collide with it), not a new location/tab type, so every existing path-based mechanism
(navigation, history, session persistence, tag-panel ancestor walking) works unmodified with no
Port/interface change — only `StandardFileSystemRepository` special-cases the sentinel in
`listDirectory()`/`stat()`, preserving "OS-specific code goes through the Port" (§6); a future Linux
backend implements its own equivalent behind the same sentinel.

Drives/quick-access folders are synthesized `FileNode`s pointing at their real paths (so
double-click, tagging, etc. work unmodified), with a `displayName()` override for drives (computed
by a small, independently unit-tested `DriveLabel` helper, since a drive root's `.filename()` is
otherwise empty). `goUp()` from a filesystem root now lands on the sentinel instead of no-op'ing;
the address bar/tab title show "This PC". The tag panel and background context menu treat the
sentinel as having no valid target. A fresh install (or a newly-revealed empty pane) now lands on
"This PC" instead of the user's home directory. Kept synchronous on the UI thread (§5). Live drive/
known-folder enumeration itself isn't covered by an automated test (§11's carve-out); the pure
`DriveLabel`/sentinel-`stat()` logic is.

### 14.16 Multi-Selection

Replaces the single-item selection §14.9/§14.10 originally shipped with real Explorer-style
multi-selection — rubber-band drag, Ctrl/Shift-click — built entirely on Qt's own
`ExtendedSelection` + `SelectRows` modes rather than custom mouse handling.
`TabViewModel::selectedEntries()` becomes a `std::vector<FileNode>`; clipboard, delete, and the
context menu's Cut/Copy/Delete-equivalent entries act on the whole selection. Single-target actions
(Rename, Properties, Open) stay single-target and are disabled for a multi-selection, since their
backing APIs are single-path. The tag panel still requires exactly one selected item (or none,
meaning the folder) — multi-item tagging isn't built. No automated coverage (§11, UI layer).

### 14.17 Tag Manager Dialog

A standalone modal (`TagManagerDialog`/`TagManagerViewModel`) for global tag CRUD — rename, delete
(cascading via the schema's `ON DELETE CASCADE`, §7), add without also tagging anything — distinct
from the tag panel (§14.9), which only attaches/detaches tags to a specific selection. Built
entirely on `TagManagementUseCase`'s existing `createTag`/`updateTag`/`deleteTag`/`searchTags`; no
Domain/Application/schema change. Reuses the §14.9 `FlowLayout`/`TagChipWidget` machinery. No
automated coverage beyond the already-tested use case (§11).

### 14.18 Filename Search (Per-Tab, Recursive)

Recursive filename/extension search scoped to a tab's current folder, triggered from a toolbar
search box and rendered inside the tab it ran against (search state lives on `TabViewModel`, not a
separate panel). `IFileSystemRepository::listDirectoryRecursive` (new, symmetric to
`listDirectory`) walks via `fs::recursive_directory_iterator`, skipping unreadable subtrees rather
than failing the whole call; it rejects the "This PC" sentinel outright. A tab owns a second
`FileListModel` for results plus a cached recursive snapshot: the initial Enter triggers one disk
scan, and further typing re-filters the cached snapshot only — no further I/O. Reuses
`FileBrowserView`/`FileListModel` as-is (icons, sorting, multi-selection, hotkeys, context menu all
come for free). Synchronous on the UI thread (§5) — a very large folder briefly freezes the UI on
the initial scan, accepted. Pure filtering logic and the repository method are GTest-covered; the
ViewModel/UI state is not (§11).

### 14.19 Advanced (Criteria) Search Pane

A second, richer search entry point alongside §14.18 (kept unmodified) — Name / Min size / Max size
/ Extension criteria, results always rendered in a taller, match-highlighting Details-like view
regardless of the tab's own `ViewMode`. Opening the pane shows a plain non-recursive listing of the
current folder (not a search — no snapshot cached, no criteria applied) so it doesn't open empty;
the first actual search (Search press, Enter, or a tag-search click) is what triggers the real
recursive scan. Mutually exclusive with quick search on the same tab; each
mode's state persists independently while hidden. `SearchCriteria` is a plain Application-layer
struct; `FileNavigationUseCase::filterByCriteria` composes three new pure filters
(`filterBySizeRange`, `filterByExtensions`, plus the existing `filterByName`), each a no-op when its
part of the criteria is unset. `TabViewModel` gets a parallel search-state block (its own
`FileListModel`, cached snapshot, criteria) alongside §14.18's, with each mode's entry point exiting
the other. `SearchResultDelegate` bolds/highlights the matched substring in the Name column, the
same manual multi-`drawText` technique `FileTileDelegate` uses. `FileBrowserView` gains an opt-in
`AdvancedSearchResults` display mode rather than a new widget class, so existing
selection/hotkey/context-menu wiring is reused as-is. Synchronous on the UI thread (§5). The pure
filters are GTest-covered; the ViewModel/UI state is not (§11). Not built: a "Location"
disambiguation column; cancellation of the recursive scan; persisting criteria across restarts
(resets like §14.18's quick search). Tag criteria (a fifth, ANDed criterion) are covered
separately by §14.22, layered on top of this section rather than modifying it.

### 14.22 Tag-Integrated Advanced Search

Extends §14.9 (tag panel) and §14.19 (advanced search) so the two features interoperate: every
tag chip anywhere in `TagPanelWidget` is clickable, and clicking one opens Advanced Search on the
*focused* tab with that tag applied as a criterion; Advanced Search itself gains its own tag
search box so criteria can be built without visiting the tag panel at all. Search results must
then satisfy file-property criteria (name/size/extension, §14.19) **and** tag criteria together.

* **`SearchCriteria` gains `std::vector<Tag::Id> tagIds`** (AND semantics, mirroring
  `ITagRepository::findFilesWithAllTags`; empty = no tag filter).
* **`FileNavigationUseCase::filterByPaths(files, allowedPaths)`** — a new pure, static filter
  (no Port dependency added to `FileNavigationUseCase`) that keeps only entries whose `path()`
  exactly matches one of `allowedPaths`. `TabViewModel` is the composition point: it calls
  `TagManagementUseCase::findFilesWithAllTags(tagIds)`, turns the returned
  `FileTagAssociation`s into a path list, and applies this filter *after* `filterByCriteria` —
  the two stages compose as an AND, same as the SQL-level AND `findFilesWithAllTags` already
  does across tags themselves. Matching is path-exact (case-sensitive), not hash-based: scanned
  `FileNode`s from `listDirectoryRecursive` never carry a populated hash (hashing is on-demand,
  only at tag-assignment time), so joining tag associations against a live recursive scan can
  only go through the path both sides share — the same path-first posture `ITagRepository`
  already uses elsewhere, just without the hash fallback (unavailable here without an expensive
  per-file recompute across the whole scan).
* **`TabViewModel` gains a `TagManagementUseCase&` dependency** (threaded through
  `WorkspacePaneViewModel`'s and `WorkspaceController`'s constructors, which already receives one
  for the shared `TagListViewModel`), plus:
  * `addTagSearchCriterion(Tag::Id)` / `removeTagSearchCriterion(Tag::Id)` — mutate
    `SearchCriteria.tagIds` and (re)run the search; opens the panel first if it wasn't already
    active (same single-active-mode invariant as `showAdvancedSearchPanel`).
  * `searchTagsForCriteria(query) const` / `resolveTagCriteria() const` — pure queries (no state
    mutation), the same posture as §14.20's `suggestFolders`: the widget layer calls them
    on-demand and pushes the result back into itself, rather than a ViewModel class mediating.
  * A shared private `runAdvancedSearch(forceRescan)` now backs `startAdvancedSearch`/
    `updateAdvancedSearchCriteria` *and* the two methods above, re-scanning only when forced or
    never yet scanned (`m_advancedSearchRoot.empty()`) — preserving §14.19's "editing before the
    first Search does not scan" contract while letting the new tag-click entry points (which have
    no Search-button press to hang a scan off of) trigger that first scan themselves.
    `showAdvancedSearchPanel`'s own non-recursive current-folder listing (§14.19) is separate from
    this cache and leaves `m_advancedSearchRoot` unset, so the first real search here still forces
    a full recursive scan.
  * A new `advancedSearchCriteriaChanged(const SearchCriteria&)` signal, since criteria can now
    change from three independent places (the existing Name/size/extension fields, Advanced
    Search's own new tag search box, and a `TagPanelWidget` click on a different widget entirely)
    that all need the tag-criteria chip row to stay in sync.
* **`TagChipWidget` becomes clickable for every `Kind`**, not just `Selectable` — `ReadOnly`/
  `Addable`/`Removable` now also emit `clicked(Tag::Id)` on a body click (unaffected: clicks on
  the separate `+`/`x` child `QToolButton`s). This one change powers both the right panel
  (requirement) and Advanced Search's new "matching tags" row, which reuses `Kind::ReadOnly`
  verbatim ("same as the right panel but without a + button").
* **`TagListViewModel::requestTagSearch(Tag::Id)`** — new slot, forwards to
  `m_activeTab->addTagSearchCriterion(tagId)`. Since `WorkspaceController::retargetFocusedTab()`
  already keeps `TagListViewModel::m_activeTab` pointed at the focused pane's active tab (§14.9),
  this automatically targets "the currently active tab" with no new retargeting logic.
* **`SearchCriteriaPanel`/`SearchResultsPane`** gain a tag search box (results shown as
  `Kind::ReadOnly` chips, click-to-add) and a tag-criteria chip row (`Kind::Removable`,
  click-x-to-remove), wired in `WorkspacePaneWidget::addPageForTab` the same way the existing
  Name/size/extension fields are.
* **Not built:** persisting tag criteria across restarts (matches §14.19's existing exclusion for
  its other fields); surfacing which specific tag caused a given result to match. No new
  automated coverage beyond the pure `filterByPaths` filter — `TabViewModel`/widget changes stay
  under §11/§14.19's existing "ViewModel/UI is not CTest-covered" posture.

### 14.20 Address Bar Folder Autocomplete

Each pane's address bar (`AddressBarWidget`, a `QLineEdit` subclass replacing the previous bare
`QLineEdit`) shows a popup of matching child folders as the user types, filtered to directories only
via `FileNode::isDirectory()`, plus inline autocompletion of an unambiguous common prefix. Suggestion
data flows the same route as navigation: `AddressBarWidget` never calls `FileNavigationUseCase`
directly, it emits `folderSuggestionsRequested(directory)` (debounced ~120ms, only on a directory-
portion change, not every keystroke) which `WorkspacePaneWidget` answers via a new
`TabViewModel::suggestFolders(directory)` (a pure, side-effect-free query alongside `navigateTo`,
using the same synchronous `FileNavigationUseCase::listDirectory` call already used by every
navigation path). Prefix filtering against what's typed happens client-side in `AddressBarWidget`
against a per-directory cache, so extending/shrinking the typed name within the same directory needs
no further disk I/O. The popup is a non-activating, frameless `QListWidget` positioned under the
address bar; `AddressBarWidget` keeps keyboard focus itself and intercepts Up/Down (move the popup's
highlighted row), Enter (commits a highlighted row and navigates, same as typing a full path and
pressing Enter), Tab (completes the text without navigating), and Escape (dismisses the popup).
Synchronous on the UI thread (§5), same accepted posture as §14.18/§14.19. Not built: suggestions
for a bare drive letter before its trailing separator; async/background listing (only relevant for
very large directories, same tradeoff already accepted for recursive search). Untested per §11 (pure
UI/ViewModel glue, same as the rest of `TabViewModel`/`WorkspacePaneWidget`).

### 14.21 Hidden Files/Folders Visibility

A global "Show hidden files" toggle in the View menu controls whether OS-hidden entries appear at
all, and if so, whether they're visually distinguished. `FileNode` gains `isHidden()`, populated by
`StandardFileSystemRepository::buildFileNode()` via `GetFileAttributesW`/`FILE_ATTRIBUTE_HIDDEN` on
Windows (a leading-`.` filename convention on a future Linux backend); every existing
`FileNode`-producing repository call funnels through that one function, so no other adapter code
changes. Synthetic nodes ("This PC", drives, known folders) are never marked hidden.

`FileListModel` is the single filter/style point rather than the Application layer or
`TabViewModel`: it already backs normal browsing, §14.18 quick search, and §14.19 advanced search
alike (one instance each, all fed via `setEntries()`), so implementing it once there covers all
three. It keeps a `m_visibleRows` index over its full `m_entries`, rebuilt whenever entries/sort/the
toggle change; with the toggle on, every row stays visible; with it off, hidden rows are dropped
from the index entirely. Text/font styling of hidden rows is not hardcoded here — it's one of
§14.29's decoration mechanisms: the built-in "Hidden Files"/"Hidden Folders" rows, user-configurable
from `Settings → General → Colors` like any other decoration rule, with no styling applied by
default until the user sets one. Qt's stock `QStyledItemDelegate` (List/Details views) honors
`Qt::ForegroundRole`/`Qt::FontRole` automatically; the three custom delegates that hand-paint text
(`FileIconDelegate`, `FileTileDelegate`, `SearchResultDelegate`) each read the same roles for their
non-selected pen color/font instead of the hardcoded `option.palette.text()` they used before, so
all seven view modes plus the advanced-search results view render consistently.

The **icon** stays hardcoded-dimmed regardless of decoration rules, unlike text/font: `data()`'s
`Qt::DecorationRole` branch runs a hidden entry's icon through a `dimmedIcon()` helper that rebuilds
it one size at a time via `QStyle::generatedIconPixmap(QIcon::Disabled, ...)` — the same graying Qt
applies to a disabled toolbar action, so it already tracks the light/dark palette without needing a
color to be configured. This is deliberate: §14.29 only ever exposed a hex color/font, never an icon
override, and Explorer-style dimmed icons are expected even when a user hasn't set (or has cleared)
the Hidden Files/Hidden Folders text style, unlike the text color which intentionally starts at
"no override" (see §14.29).

The toggle itself is a `QSettings`-backed app-wide preference (`MainWindow`, modeled on
`createContextMenuModeAction()`) rather than `config.json`/`WorkspaceConfig` state — same posture as
§14.13's shell-extensions toggle, since it's one app-wide display preference, not per-tab/session
state. Toggling it persists the value and live-broadcasts to every open tab's three `FileListModel`s
(main/quick-search/advanced-search) across all four panes via `WorkspaceController::pane(id)`/
`tabAt(i)`, so already-open folders and result sets update immediately with no re-navigation or
rescan; a `FileListModel` constructed afterward (a new tab) picks up the persisted value itself.
§14.20's `TabViewModel::suggestFolders` also excludes hidden folders from autocomplete when the
toggle is off, reading the same setting on demand (autocomplete is already re-queried per keystroke,
so no live-broadcast plumbing is needed there). Not built: a per-item way to toggle a file's own OS
hidden attribute (Properties-dialog territory); any Linux-specific hidden-file semantics beyond the
one-line dotfile fallback above. Untested per §11 beyond the `FileNode`/`StandardFileSystemRepository`
attribute-detection cases (the rest is UI/ViewModel glue, same posture as §14.18/§14.19/§14.20).

### 14.22 Startup/Exit Logging and Fatal Exception Handling

A `Logging` module (`adapters/logging`) wraps spdlog behind `Logging::init/shutdown/log` (Debug/
Info/Warning/Error, timestamped, rotating file sink under `%LOCALAPPDATA%/Exp-LORer/logs/` plus a
console sink in debug builds) — the sole spdlog call site (§6). At this stage it is called only
from `main.cpp`, logging one line at startup (after `init()`) and one at clean exit (before
`shutdown()`); no call sites exist yet elsewhere in the app.

Any uncaught C++ exception is caught, logged at Error level, and shown to the user in a modal
dialog (title, exception message, a single "Exit" button) before the process terminates —
`ExceptionHandler::handleFatal` (`src/app`), invoked from two places: a top-level `try/catch`
around `main()`'s body (covers setup before the event loop starts, e.g. `SQLiteTagRepository`
failing to open the DB) and `ExpLorerApplication::notify()` (a thin `QApplication` subclass
overriding `notify()` to catch exceptions thrown during Qt event dispatch — input events, paint
events, queued slot invocations — while `app.exec()` is running).

**Scope:** main-thread exceptions only, consistent with all Port calls still being synchronous on
the UI thread in v1 (§5) — there is no second thread whose exceptions would need separate handling
yet. **Not built:** call-site logging beyond the two startup/exit lines above; an `ILogger` Port for
Domain/Application to log through (deferred until those layers actually need to log — `Logging` is
plain infrastructure, not wired through the composition root); native/SEH crash handling for
non-C++-exception crashes (e.g. access violations); per-thread exception handling for a future
background-dispatch thread (§5's deferred background-dispatch work would need its own try/catch at
each thread's entry point, since exceptions can't cross threads). `ExceptionHandler`/
`ExpLorerApplication` are UI-layer bootstrap code, out of CTest scope per §11 (same posture as the
other UI carve-outs) — smoke-tested manually; `Logging` itself gets an adapter test (`:memory:`-style
real temp directory, per §11) verifying `init()`/`log()`/`shutdown()` produce a log file with the
expected content.

### 14.23 Bottom Panel and Integrated Terminal

A VS Code-style collapsible panel at the bottom of every tab's own page (this app's tab, i.e. one
page of a pane's `QTabWidget` — not a top-level window), showing a strip of "panel tabs"; only
"Terminal" exists in v1, built so more (e.g. Problems/Output) can be added later without new
plumbing. Collapsed by default; the first time a tab's Terminal panel tab is clicked, a `cmd.exe`
session is lazily spawned in that tab's current folder at that moment — it does not follow later
navigation in the same tab, and is not restarted by later clicks (collapsing/re-expanding just
hides/shows the still-running session). Closing the tab kills the process.

* **No Application-layer Port.** Like `Logging` (§14.22), this has no Domain/Application business
  logic to abstract behind an interface, so it's plain UI-adjacent infrastructure with no
  `CompositionRoot`/use-case involvement.
* **`src/adapters/terminal`** (new module, `explorer_adapters_terminal`): `AnsiTerminalBuffer` — pure
  logic (no Qt Widgets, no OS calls), a resizable cell grid + cursor + capped scrollback, fed raw
  bytes via `feed()` and interpreting a practical VT/ANSI subset (cursor motion, erase-in-line/
  display, insert/delete/erase-character (ICH/DCH/ECH — needed for e.g. `cmd.exe`'s in-place line
  redraw during history recall, which shifts/erases characters rather than always erase-and-
  rewriting the whole line), 16-color SGR) — no 256-color/truecolor. The one piece of this feature
  that's pure and GTest-covered (`tests/adapters/AnsiTerminalBufferTest.cpp`), same posture as
  `DriveLabel`/
  `WorkspaceLayoutTopology`. `WindowsConPtyProcess` (Windows-only, same "Windows first" posture as
  `ShellContextMenuProvider`) — a `QObject` wrapping ConPTY (`CreatePseudoConsole` + `CreateProcessW`
  of `%ComSpec%`); runs one background `std::thread` blocking on `ReadFile` of the output pipe,
  posting each chunk back to the UI thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
  as `dataReceived(QByteArray)` — the only cross-thread hop, keeping ConPTY's blocking I/O off the UI
  thread (§5); emits `processExited(int)` when the pipe closes. `writeInput`/`resize` are synchronous
  UI-thread calls (`WriteFile`/`ResizePseudoConsole`), the same accepted posture as `ShellExecuteW`
  (§14.11). No `IPtyProcess` interface: Qt doesn't support pure-virtual signals, and nothing outside
  the UI layer needs to mock this (§11 already excludes UI from CTest) — a future Linux
  `PosixPtyProcess` is deferred until Linux support is actually built, at which point an interface
  can be introduced alongside it.
* **`src/ui/widgets`**: `TerminalWidget` — owns one `AnsiTerminalBuffer` + one `WindowsConPtyProcess`,
  paints the grid with a monospace font, translates key events to VT byte sequences, and resizes the
  pty on `resizeEvent`. On process exit, shows an inline restart prompt rather than auto-restarting.
  It also owns keyboard-only text selection and clipboard state — an anchor/active pair of
  `{logicalRow, column}` (logical row spanning scrollback + live grid, the same indexing
  `paintEvent` already uses to address either) tracked entirely in `TerminalWidget`, not
  `AnsiTerminalBuffer`, since it's interaction/rendering state rather than terminal emulation state:
  Shift+Left/Right extends a character selection, Ctrl+Shift+Left/Right a word selection (word
  boundaries computed within one row only — no cross-line word semantics) while *also* forwarding
  the word-jump VT sequence (`CSI 1;5D`/`CSI 1;5C`) so the shell's line-edit cursor moves in step
  with the extending selection. The local word-jump mirrors the console's own two-phase word-left/
  word-right (skip the adjacent whitespace run, then skip the word run beyond it, both within one
  keypress) rather than stopping at the first run boundary — a single-phase skip lands half a word
  short of where the shell's own cursor ends up, and the two silently drift apart over repeated
  presses. On the live input row, backward selection (both Shift+Left and
  Ctrl+Shift+Left) additionally stops at the shell prompt instead of walking into the
  current-directory text before it — the first `>` on that row (an illegal character in a Windows
  path, so it can only be the default `$P$G` prompt's terminator) is treated as a hard left
  boundary. Symmetrically, forward selection (Shift+Right and Ctrl+Shift+Right) stops at the end of
  the typed command instead of walking into the untyped blank cells that fill out the rest of the
  row — the boundary is the last non-space column on that row, widened to the selection anchor's own
  column when the anchor sits on that row (the anchor is planted at the cursor cell, one column past
  the last typed character, so without the widening a backward selection could never shrink all the
  way back to its own starting point via Shift+Right). Ctrl+A selects the whole retained buffer, Ctrl+Shift+C/V copy/paste
  via `QGuiApplication::clipboard()`, and Ctrl+Left/Right (no Shift) forwards the same word-jump
  sequence without touching any local selection (clearing one if active). Deliberately
  keyboard-only — no mouse drag-to-select.
  `BottomPanelWidget` — the generic collapsible panel shell: a header strip of checkable panel-tab
  buttons over a `QStackedWidget` of panel-tab content, starting collapsed; emits `expansionChanged`
  (for the owning splitter to resize) and `panelTabFirstActivated` (for lazy terminal start), staying
  itself splitter-agnostic. `WorkspacePaneWidget::addPageForTab` wraps its existing per-tab
  browse/search/advanced-search `QStackedWidget` and a `BottomPanelWidget` in a vertical `QSplitter`
  as the actual tab page; `onViewModeChanged`/`currentColumnWidths` unwrap `splitter->widget(0)`
  instead of assuming the tab page is the `QStackedWidget` directly.
* **Not built:** a Linux backend; mouse drag-to-select (selection is keyboard-only, see above);
  multiple terminal instances per tab; 256-color/truecolor; persisting panel open/closed state or
  size across restart (no `WorkspaceConfig`/§14.14 involvement — resets every launch); the
  terminal's directory following subsequent navigation in its tab (only the directory at
  first-expand time). No automated coverage beyond `AnsiTerminalBuffer` — ConPTY spawning and the
  painting/input/selection/clipboard widget code fall under the existing §11 UI/OS-shell carve-out,
  smoke-tested manually.

### 14.24 Right Panel (Tabbed, Collapsible)

The window's right-hand side panel uses the same VS Code-style collapsible-panel shell as the
bottom panel (§14.23), on the horizontal axis instead of the vertical one. `RightPanelWidget`
(`src/ui/widgets`) mirrors `BottomPanelWidget`'s mechanics — checkable panel-tab buttons over a
`QStackedWidget`, starting collapsed, `expansionChanged`/`panelTabFirstActivated` signals,
splitter-drag detection in `resizeEvent` — but its header strip is a vertical column of buttons on
the widget's left edge (next to the workspace) rather than a horizontal row on top, since the
collapse axis is width (`collapsedWidth()`/`preferredExpandedWidth()`) rather than height. It is a
sibling class, not a shared abstraction with `BottomPanelWidget`: the two header strips lay out
buttons in opposite directions and the two widgets sit in splitters of opposite orientation, so
forcing one orientation-parameterized base class for two call sites wasn't worth the indirection.

`MainWindow::createWorkspace` wraps the existing `TagPanelWidget` (§14.9/Specification.md "Tag
Panel") as the content of a single "Tags" panel tab, added to a `RightPanelWidget` placed as the
second child of the central horizontal `QSplitter` (in place of `TagPanelWidget` directly). The
splitter uses explicit `setSizes`/`setCollapsible(0, false)` plus a `RightPanelWidget::
expansionChanged` handler that resizes the splitter, the same posture as the bottom panel's
splitter wiring in `WorkspacePaneWidget::addPageForTab` — no stretch factors. `TagPanelWidget`
itself is unmodified; only its container changed. Only "Tags" exists in v1, but more tabs can be
added later without further plumbing, same intent as the bottom panel's Terminal-only v1 scope.
Like the bottom panel, expanded/collapsed state is not persisted across restarts.

### 14.25 Right-Panel File Preview (Preview Tab)

A second `RightPanelWidget` tab, "Preview" (§14.24's panel already supports more than one tab with
no further plumbing), showing content for whatever the focused pane/tab currently has selected —
or its browsed folder when nothing is selected, same resolution rule `TagListViewModel::
resolveTarget()` already uses for the tag panel (§14.9). This is also the first concrete
implementation of `adapters/media` (previously `.gitkeep` only) and the first feature to actually
do background-thread Port dispatch (§5).

* **Scope by file type:** a directory shows its immediate children only (directories first, then
  files, both alphabetical — not a recursive tree); a recognized text/source extension
  (txt/html/xml/json/md/ini/log/yaml/csv, common source extensions cs/cpp/h/hpp/c/py/js/ts/css/…)
  shows a capped byte-prefix of its content, falling back to "unsupported" if a `\0` appears in
  that prefix (binary guard); an image extension (jpg/png/bmp/gif/webp) shows a scaled preview
  decoded via **libvips** (new vcpkg dependency), per the product requirement to use that library
  specifically rather than Qt's own image codecs; a video extension (mp4/mkv/avi/mov/…) shows a
  scaled **static poster frame** decoded via FFmpeg (already a dependency) — no inline playback,
  which belongs to the still-unbuilt full-window QML viewer (Specification §2.3/§2.4) instead.
  Anything else shows "no preview available."
* **`FilePreviewUseCase`** (new, `src/application`) — stateless, depends on
  `IFileSystemRepository` (folder listing via the existing `listDirectory`; a new
  `readFilePrefix(path, maxBytes)` method for text) and `IMediaDecoder` (image/video thumbnail
  bytes, reusing the existing `generateThumbnail(path, maxWidth, maxHeight)` contract) — never on
  `MediaProcessingUseCase` or any other use case, preserving this layer's "depend on Ports only"
  testability posture (§11). Returns a new Domain entity, `FilePreview` (kind + folder
  entries/text/image-or-poster bytes).
* **`adapters/media`** (new target `explorer_adapters_media`): `MediaDecoder` — the concrete
  `IMediaDecoder` dispatching by extension to `VipsImageDecoder` (images, libvips end to end:
  decode, resize, JPEG-encode) or `FFmpegMediaDecoder` (video, one representative frame via
  libavformat/libavcodec, scaled via libswscale, JPEG-encoded via libavcodec's MJPEG encoder) —
  each self-contained, not cross-calling the other, keeping the two backends independently
  swappable. `ThumbnailCache` + `CachingMediaDecoder` (§8) sit in front as a transparent decorator,
  so `FilePreviewUseCase` never sees caching logic.
* **`FilePreviewViewModel`** (new, `adapters/viewmodels`) — the shared ViewModel, owned by
  `WorkspaceController` and retargeted via `setActiveTab()` exactly like `TagListViewModel` (§14.9)
  — a **distinct component from the still-unbuilt `MediaPreviewViewModel`** named in §2.3, which is
  QML-facing state for the future full-window viewer, not this panel; the two must not be
  conflated. Owns one dedicated background `QThread` + `FilePreviewWorker` (one always-alive
  worker, not a pool — only one active preview needed at a time), the same "one background thread,
  results marshaled back via a queued signal" shape `WindowsConPtyProcess` already established
  (§14.23). A `setPanelActive(bool)` slot, driven by the widget's `showEvent`/`hideEvent`, stops
  background work while the Preview tab isn't the visible one and catches up immediately on
  reactivation. Cancellation is a generation-id fence (§5) — switching the selection bumps a
  counter, and a result that arrives for a stale generation is dropped, never shown; the
  in-flight libvips/FFmpeg call itself still runs to completion, since neither library exposes a
  mid-call abort. A small in-memory LRU (~20 entries) caches `Text`/`Folder` results only —
  image/video results are already cached on disk by `CachingMediaDecoder`, so caching them a
  second time here would be redundant.
* **`PreviewPanelWidget`** (new, `src/ui/widgets`) — a `QStackedWidget` switching between an image/
  poster `QLabel`, a read-only monospace `QPlainTextEdit` for text, a flat `QListWidget` (existing
  `QFileIconProvider` icons) for the one-level folder listing, and loading/unsupported/error
  states, driven by `FilePreviewViewModel`'s signals. `MainWindow::createWorkspace` adds it as a
  second `RightPanelWidget` tab next to "Tags" — no change needed to `RightPanelWidget` itself.
* **Not built:** video playback; a recursive/multi-level folder tree; animated-GIF playback beyond
  a static first frame (goes through the image path); full-content display beyond the byte cap
  (syntax highlighting of the shown prefix is covered by §14.26); persisting the Preview tab's
  expanded/collapsed state (matches §14.23/§14.24's existing unpersisted posture); a Linux
  media-decoding backend.

### 14.26 Text-Preview Syntax Highlighting

Recognized text/source files shown in the Preview tab (§14.25) are syntax-highlighted using
tree-sitter as the parsing engine, plus a `File > Settings...` dialog letting the user recolor each
language's tokens. Like the integrated terminal (§14.23), tree-sitter is a plain C library — not
Qt/SQLite/FFmpeg — but this is a presentation concern, not a Domain/Application business rule, so
**no Application-layer Port and no `CompositionRoot` use-case wiring** are introduced.

* **Grammar sourcing.** vcpkg's public registry ships only tree-sitter's core C library plus a
  `tree-sitter-c` grammar — no C++/C#/Python/JS/TS/JSON/HTML/CSS grammars. Those are sourced via
  small **vcpkg overlay ports** (§6) — `vcpkg-overlays/ports/tree-sitter-<lang>/`, one per grammar,
  each fetching that grammar's upstream release archive (`vcpkg_from_github`, pinned tag) and
  compiling its `src/parser.c` (+ `src/scanner.c`/`.cc` where present, e.g. C++/TypeScript/Python)
  into a static lib, installing the header plus that grammar's own `queries/highlights.scm`.
* **Language scope (this pass):** C, C++, C#, Python, JavaScript, TypeScript, JSON, HTML, CSS. The
  remaining text extensions §14.25 already accepts (txt/xml/md/ini/log/yaml/csv/sql/sh/bat/ps1/
  xaml/toml) keep rendering as plain, unhighlighted text — a documented scope exclusion, not a
  limitation of the mechanism (more languages are added by vendoring more overlay-port grammars).
* **`src/adapters/highlighting`** (new, pure logic, no Qt except one resource-loading function):
  `Language` (plain enum, one per supported language); `LanguageRegistry::languageForExtension`
  (extension → optional `Language`, same static-dispatch shape as `MediaExtensions`);
  `SyntaxHighlightEngine` — owns one `TSParser`/compiled `TSQuery` per `Language` (lazy-built,
  query text loaded from that language's bundled `highlights.scm` via Qt resources, the same
  mechanism `resources/qml.qrc` already uses). `highlight(Language, source)` returns
  `{byteOffset, byteLength, captureName}` spans; `captureNamesFor(Language)` returns exactly the
  capture names that language's own `highlights.scm` defines (e.g. CSS has no `function` capture),
  so the Settings dialog never shows a hardcoded universal token list. GTest-covered like
  `AnsiTerminalBuffer` (`tests/adapters/SyntaxHighlightEngineTest.cpp`) — no Qt/disk/SQLite touched.
* **`src/adapters/config`** gains a second store, deliberately separate from `AppConfig`/
  `AppConfigStore` because it is a persistent user *preference* (edited from Settings, saved
  immediately) rather than session/window state (saved only on close, §14.14): `HighlightTheme` — a
  language → capture-name → hex-color map, seeded with built-in defaults (one shared base palette:
  keyword/blue, string/green, comment/gray, number/teal, function/purple, type/cyan,
  variable/operator/default-foreground) wherever the user hasn't overridden a token —  and
  `HighlightThemeStore`, the same tolerant-load/atomic-save shape as `AppConfigStore`, its own
  `highlight_theme.json` alongside `config.json`/`explorer.db` under the app-data directory.
* **`src/ui/widgets`:** `SyntaxHighlighter` (`QSyntaxHighlighter` subclass) attached to
  `PreviewPanelWidget`'s text view — `showText()` sets the active `Language` from the previewed
  path's extension (unmapped extensions stay unhighlighted, as today); since preview text is capped
  and read-only, spans are computed once per preview load, not per keystroke, and a `themeChanged()`
  signal triggers `rehighlight()` so an open preview updates live from Settings. `SettingsDialog`
  (new) — a `QTreeWidget` category tree on the left (Highlight → Languages → one entry per
  `Language`) and a `QStackedWidget` on the right listing the selected language's capture names
  (from `captureNamesFor`) each with a `QColorDialog`-backed color swatch; a single "Close" button,
  no OK/Cancel, edits apply and persist immediately (same posture as `TagManagerDialog`'s tag
  edits) — structured generically so a future non-highlighting settings category slots in without
  reshaping the dialog. `HighlightThemeViewModel` (new, `adapters/viewmodels`) wraps
  `HighlightTheme`/`HighlightThemeStore` (`tokenColor`/`setTokenColor`/`themeChanged`), owned once
  by `CompositionRoot`/`MainWindow` like `TagListViewModel` so `SettingsDialog` and every
  `SyntaxHighlighter` observe the same live theme. `MainWindow::createMenuBar()` adds
  `File > Settings...`, opening `SettingsDialog`. `SettingsDialog` also hosts a second, non-
  highlighting category — General > UI, one `QCheckBox` ("Show close button on tabs") — exercising
  the "future category slots in without reshaping the dialog" extension point above: its own
  `QSettings`-backed key (`WorkspacePaneWidget::kShowTabCloseButtonsSettingsKey`,
  `"UI/ShowTabCloseButtons"`, default on) and a `showTabCloseButtonsChanged(bool)` signal that
  `MainWindow` fans out to every pane's `WorkspacePaneWidget::setTabCloseButtonsVisible()`, same
  propagation shape as the existing "Show hidden files/folders" View-menu toggle (§14.21).
  Separately, `src/ui/widgets/UiColors.h` (new, header-only) centralizes the fixed, muted-blue hex
  constants used for panel/tab visual-distinction styling (active pane/tab tint, bottom/right panel
  header tint, advanced-search button, tags-panel section backgrounds) — plain `setStyleSheet` calls
  scoped by `objectName`, not a theming engine; the app has no dark-mode/theme-switching support.
* **Not built:** highlighting for the plain-text extensions listed above; highlighting anywhere
  outside the Preview panel (e.g. not inside the §14.23 terminal); theme import/export, light/dark
  presets, or a "reset to defaults" button; automated UI coverage for `SettingsDialog`/
  `SyntaxHighlighter` beyond `SyntaxHighlightEngine`'s own GTest coverage (§11's existing UI/
  ViewModel-glue carve-out).

### 14.27 Per-Pane Status Bar

A persistent status bar shown at the bottom of every pane, reporting the currently focused
tab's object count / selection count and hosting a quick-select field — distinct from
`MainWindow`'s existing `QMainWindow::statusBar()` (a transient, window-wide 5-second
auto-hiding message bar for navigation/search failures, `MainWindow::onStatusMessage`), which is
unchanged by this feature.

* **One widget per pane, rebound to the active tab** — the same shape `WorkspacePaneWidget`
  already uses for its toolbar (address bar, back/forward/up): a single `StatusBarWidget`
  instance, not one per tab page, so the existing per-tab splitter/`QStackedWidget` structure
  (`addPageForTab`, §14.23) is untouched. `bindToolBarToTab()` — already the tab-switch rebind
  point — additionally wires the new tab's `directoryContentsChanged`, `searchResultsChanged`,
  `advancedSearchResultsChanged`, `searchModeChanged`, `advancedSearchModeChanged`, and
  `selectedEntriesChanged` signals to a `refreshStatusCounts()` handler that reads the counts
  directly off the tab (`advancedSearchActive()`/`searchActive()` pick which of
  `fileListModel()`/`searchResultsModel()`/`advancedSearchResultsModel()`'s `rowCount()` is
  "current", plus `selectedEntries().size()`), so it's correct regardless of which signal fired.
* **`StatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`: a `QLabel` (`"<N> items"`, or `"<N> items, <M> selected"` once something is
  selected) plus a `QLineEdit` quick-select field emitting `quickSelectTextChanged(QString)` on
  every keystroke; Escape clears it. No `TabViewModel`/`FileListModel` dependency — it only
  emits signals, `WorkspacePaneWidget` mediates, exactly like the address bar's
  `folderSuggestionsRequested` (§14.20). Fixed height, appended below `m_tabWidget` in
  `WorkspacePaneWidget`'s own top-level layout (not inside any per-tab splitter).
* **Quick select** (`FileBrowserView::selectEntriesContaining(text)`, new) — a linear scan over
  the currently active tab page's model (mirroring `indexForPath`'s existing scan), matching
  against the Name column's *displayed* text (`Qt::DisplayRole`, so synthetic `displayName()`
  overrides like "This PC" drives, §14.15, match what's typed) rather than `FileNode::name()`
  directly. Every case-insensitive substring match (anywhere in the name, not just a prefix) is
  selected; the first match is scrolled into view. Empty text is a no-op (leaves the existing
  selection alone); no match clears the selection. `WorkspacePaneWidget` resolves "the currently
  active tab page's browser view" via a new `currentBrowserView()` helper, factored out of the
  unwrap `onViewModeChanged()`/`currentColumnWidths()` already duplicated (splitter →
  `QStackedWidget` → `FileBrowserView`-or-`SearchResultsPane::browserView()`).
* **Reset points:** the quick-select field is cleared (`StatusBarWidget::clearQuickSelect()`) on
  tab switch and on the bound tab's `currentPathChanged`/search-mode-toggle signals, since the
  visible item set changes completely at each of those points — same ephemeral-per-context
  posture as the quick-search/advanced-search boxes' own state (§14.18/§14.19).
* **Not built:** cycling through multiple matches (Tab/F3) — all matches are selected at once;
  persisting the quick-select text across navigation or restart; per-tab status bar instances.
  No Domain/Application change — every count and listing the bar needs already exists on
  `TabViewModel`/`FileListModel`. No automated coverage: pure UI/ViewModel glue, same carve-out
  as the toolbar and address-bar autocomplete (§11).

### 14.28 Archive Browsing, Preview, and Extraction (Read-Only)

Lets the user browse into a `.zip`/`.7z`/`.tar`/`.tar.gz`/`.tar.bz2` archive (or a **read-only**
`.rar`) as if it were a folder, preview its contents in the right panel, and extract from it — no
archive creation or in-archive editing in v1.

* **Archives as virtual paths.** Exactly the same posture as "This PC" (§14.15): a real archive
  file's own path, once entered, *is* the navigation location for everything below it — e.g.
  `C:\Downloads\photos.zip\vacation\img.jpg` is a legal `std::filesystem::path` (no real file backs
  it) that `parent_path()`/`filename()`/`/`-append all handle correctly with zero special-casing
  outside `StandardFileSystemRepository`. Every existing path-based mechanism (`TabViewModel::
  navigateTo`, history, breadcrumbs, session persistence, §14.20 address-bar autocomplete, sort/
  view-mode/multi-selection/status bar) therefore works unmodified. `Up`/`Backspace` from an
  archive's root lands on its real containing folder for free, via the same `parent_path()` call
  every other navigation already uses.
* **One new Port method**, `IFileSystemRepository::extractArchive(archiveFile,
  destinationDirectory)` (extracts everything under `archiveFile`, or under a folder-inside-an-
  archive path, preserving relative structure) — every other interaction reuses existing Port
  methods, made archive-aware only inside `StandardFileSystemRepository`:
  * `listDirectory`/`stat` on an archive-rooted path return synthesized `FileNode`s (directories
    inferred from entry-path prefixes when a format doesn't carry explicit directory entries, e.g.
    zip; size 0 and the archive's own `modificationDate` for those synthesized directories).
  * `copy(source, destination)` extracts instead of `fs::copy` only when `source` resolves *inside*
    an archive; a real, not-yet-entered archive file as `source` is unaffected (still a literal
    byte copy — Ctrl+C/Ctrl+V on a `.zip` still duplicates the `.zip`). This is what makes
    Copy→Paste out of an archive into a real folder work with no new UI plumbing, and it shares one
    internal extraction helper with `extractArchive` itself.
  * `move`/`moveToTrash`/`deletePermanently`/`createDirectory`/`createFileFromTemplate` return
    `Error(ErrorCode::InvalidArgument, "Cannot modify a read-only archive")` when the target is
    inside an archive — defense-in-depth; the UI also disables these proactively.
  * `openWithDefaultApplication` on a file entry inside an archive extracts it to a per-session
    temp directory (`%TEMP%\Exp-LORer\archive-preview\...`, not cleaned up by the app — relies on
    normal OS temp lifecycle) and launches the extracted copy — mirrors Explorer's/7-Zip's own
    zip-folder "open" behavior.
  * `listDirectoryRecursive` rejects an archive-rooted `root`, the same way it already rejects
    `VirtualPaths::ThisPC` — recursive search does not reach inside archives in v1.
* **`FilePreviewUseCase`** (§14.25): the folder-preview branch's condition widens from
  `target.isDirectory()` to `target.isDirectory() || ArchiveExtensions::isArchiveExtension(...)` —
  an archive file gets the identical `FilePreview::folder(entries)` treatment a real folder does,
  since `listDirectory()` on its own path already returns its top-level entries once the adapter
  change above lands. `PreviewPanelWidget` needs no changes — it already renders `Folder` kind.
* **`src/application/ArchiveExtensions.h`** (new, pure, mirrors `MediaExtensions.h`): the
  supported-extension list, plus a pure/lexical (no I/O) `archiveAncestorInPath(path)` used only by
  the UI layer as a cheap hint for which context-menu actions to show — the authoritative,
  disk-verified check lives in the adapter (below). A UI/adapter mismatch (e.g. a real folder
  literally named `foo.zip\`) degrades to a disabled button or a graceful `Result` failure, never a
  crash.
* **`src/adapters/archive`** (new module, `explorer_adapters_archive`, mirrors `adapters/media`):
  `ArchivePathResolver` (walks a path's ancestors for the outermost real, on-disk archive file —
  nested archives are not resolved further; navigating into an archive-looking entry inside another
  archive fails gracefully as `NotFound`, not a crash — explicit v1 exclusion); `LibArchiveReader`
  (thin wrapper over libarchive's `archive_read_*`, only the in-scope formats/filters enabled, not
  `archive_read_support_format_all()`); `ArchiveIndexCache` (small in-memory-only LRU, ~4 archives,
  keyed by `(path, size, mtime)` — same staleness-free posture as `ThumbnailCache`, §8 — so
  navigating between folders inside one archive doesn't re-scan the whole stream each time).
* **UI wiring**: `WorkspacePaneWidget::wireBrowserView`'s `itemActivated` handler navigates
  (instead of calling `openFile`) whenever the target is a directory *or* an archive-extensioned
  entry — satisfying "opening an archive doesn't launch an external app." `ContextMenuBuilder::
  NativeActions` gains two nullable actions, `extractHere`/`extractTo`, shown when every selected
  path is a real archive file; while the tab is browsing inside an archive, `cut`/`copy`/`paste`/
  `rename`/`deleteAction`/`newFolder`/`properties` are disabled (OS-clipboard copy needs a real
  `CF_HDROP` path, which a virtual archive entry isn't) and the same Extract actions target the
  in-archive selection instead. The bottom-panel terminal's cwd fallback (§14.23, already guarding
  `cwd == VirtualPaths::ThisPC`) gains the same guard for an archive-rooted `cwd`.
* **Not built:** archive creation/add-to-archive/any write support; nested-archive browsing;
  recursive/advanced search reaching inside archives; tagging a target inside an archive (disabled
  — `computeFileHash`'s rename/move-relocation fallback can't be computed for a virtual entry
  without a full extraction); guaranteed type-specific icons for in-archive entries
  (`QFileIconProvider` needs a real on-disk file; may fall back to a generic icon); RAR5 coverage
  beyond whatever the vcpkg-pinned libarchive version supports (legacy RAR4 is the safe floor).
  Adapter-layer pieces (`ArchiveExtensions`, `ArchivePathResolver`, `LibArchiveReader`, the
  archive-aware `StandardFileSystemRepository`/`FilePreviewUseCase` paths) are GTest-covered per
  §11; the UI-layer changes above fall under the existing UI/ViewModel-glue carve-out, smoke-tested
  manually.

### 14.29 Per-File/Folder Font & Color Customization (Pattern-Based)

Lets the user define an ordered list of **decoration rules** — each a comma-separated set of
wildcard name patterns plus a style (color, font family override, bold/italic/underline/strikeout)
— edited from a new `Settings → General → Colors` page. Every row in every file listing (normal
browsing, quick search §14.18, advanced search §14.19 — all seven view modes) is checked against the
rule list; the first rule whose pattern list matches wins and its style is applied to the whole row.
Like syntax highlighting (§14.26) and the terminal (§14.23), this is a pure presentation concern:
**no Domain/Application change, no new Port, no `CompositionRoot` use-case wiring** — only new
adapter-layer pure logic plus Qt-facing config/ViewModel/UI, mirroring the
`HighlightTheme`/`HighlightThemeStore`/`HighlightThemeViewModel` stack.

* **Pattern syntax:** `*` (any run of characters), `?` (exactly one character), `"..."` (a quoted
  span is taken literally, so a comma inside it doesn't split the pattern list; a doubled `""`
  inside one is a literal `"`), and a leading `#` on an individual pattern (stripped before
  matching) which marks that pattern as applying to **folders** instead of the default **files
  only**. Multiple patterns for one rule are comma-separated with OR semantics (any one matching is
  enough). Matching is against `FileNode::name()` (the last path component, not the full path),
  case-insensitive on ASCII only (non-ASCII case-folding is out of scope). Worked examples: `*.exe`
  (files only), `Image_?.png` matches `Image_1.png`/`Image_2.png` but not `Image_2121.png`, `#.git`
  (a folder named exactly `.git`), `"Image, file.png"` (one pattern containing a literal comma).
* **`src/adapters/decoration`** (new module, `explorer_adapters_decoration`, pure C++ — no Qt, same
  posture as `adapters/highlighting`'s `LanguageRegistry`): `FileNamePattern` (parses/strips the `#`
  marker, glob-matches `*`/`?` against a UTF-8 name decoded to codepoints so `?` matches one
  *character*, not one UTF-8 byte); `FileDecorationRule` (a pattern list plus style fields —
  `std::optional<std::string> hexColor`/`fontFamily`, `std::optional<int> fontPointSize`,
  `bool bold/italic/underline/strikeout` — `create()` returns a `Result` so an unterminated `"` is a
  reportable parse error, not a crash);
  `FileDecorationRules` (an ordered `std::vector<FileDecorationRule>`,
  `resolve(nameUtf8, isDirectory)` returns the first match or `nullptr` — **first-match-wins, not
  merged**, so rule order is priority and the Settings page exposes Up/Down reordering). GTest-
  covered like `LanguageRegistry`/`SyntaxHighlightEngine`.
* **Built-in Hidden Files/Hidden Folders rows (§14.21):** `FileDecorationRules` additionally holds
  two always-present `FileDecorationRule` members — `hiddenFilesRule()`/`hiddenFoldersRule()`
  (read) and `setHiddenFilesRule()`/`setHiddenFoldersRule()` (write), selected by
  `hiddenStyle(isDirectory)` — that are **structurally excluded from `resolve()`**: they're stored
  outside `m_rules` and never participate in name-pattern matching or Up/Down priority. They start
  with no color/font override (both fields unset, same "(Default)" starting point `Add Rule`
  produces) until the user sets one — there is deliberately no hardcoded hidden-dim fallback
  anymore. `FileDecorationRulesStore` persists them as two optional top-level JSON keys,
  `hiddenFiles`/`hiddenFolders`, holding the same `color`/`fontFamily`/`fontSize`/`bold`/`italic`/
  `underline`/`strikeout` fields as a rule object but no `patterns` key; missing or malformed keys
  (old-format file, hand-edited JSON) fall back to the same no-override default, matching the
  store's tolerant-load posture. `FileDecorationsViewModel` exposes matching
  `hiddenFilesRule()`/`hiddenFoldersRule()`/`setHiddenFilesRule()`/`setHiddenFoldersRule()`, each
  broadcasting `rulesChanged()` and queuing a save exactly like `setRules()`.
* **`src/adapters/config`** gains `FileDecorationRulesStore` (own `file_decorations.json`, same
  tolerant-load/atomic-save shape as `HighlightThemeStore`), `FileDecorationRulesSaveWorker` (same
  background-thread-save shape as `HighlightThemeSaveWorker`), and `FileDecorationsViewModel` (the
  live `QObject` wrapper — `rules()`/`setRules()`, `rulesChanged()` — owned once by
  `CompositionRoot`/`MainWindow` like `HighlightThemeViewModel` so
  `SettingsDialog` and every `FileListModel` observe the same live rule set).
* **Threading into every `FileListModel` — deliberately *not* a `FileDecorationsViewModel&`:**
  `src/adapters/config` already depends on `src/adapters/viewmodels` (`AppConfig` needs
  `FileListModel::ColumnCount`), so a `FileDecorationsViewModel&` constructor parameter on
  `FileListModel`/`TabViewModel`/`WorkspacePaneViewModel`/`WorkspaceController` (all in
  `src/adapters/viewmodels`) would create a build-graph cycle. Instead, the same
  `CompositionRoot::createWorkspaceController()`/`createTabViewModel()` →
  `WorkspaceController` → `WorkspacePaneViewModel` → `TabViewModel` → each of its three
  `FileListModel` instances (main/quick-search/advanced-search) chain §14.22 used for
  `TagManagementUseCase&` threads **two** plain parameters instead: `const FileDecorationRules&`
  (from `src/adapters/decoration`, which has no such dependency, for the actual name matching) and
  a bare `QObject&` — the same `FileDecorationsViewModel` instance, referenced only as its `QObject`
  base. `FileListModel` connects to that `QObject&`'s `rulesChanged()` signal via the string-based
  `SIGNAL`/`SLOT` overload of `connect()`, which needs no compile-time knowledge of
  `FileDecorationsViewModel` and so pulls in no dependency on `src/adapters/config`. On that signal,
  it re-emits `dataChanged()` across `Qt::ForegroundRole`/`Qt::FontRole` for every row, so
  already-open listings and search results restyle live the moment a rule is added/edited/removed/
  reordered — the same immediate-apply posture every other Settings page uses.
  `explorer_adapters_viewmodels` links `explorer_adapters_decoration` directly for this.
* **`FileListModel::data()`:** resolves a decoration via a single `resolveDecoration(entry)` point
  that first tries `m_fileDecorationRules.resolve(PathUtf8::toUtf8(entry.name()),
  entry.isDirectory())` — **never `path::string()`**, per §6 — skipped for synthetic rows
  (`entry.displayName()` set, e.g. "This PC"'s drives, §14.15), since they have no real on-disk
  filename to match against. If no pattern rule matches and `entry.isHidden()`, it falls back to
  `m_fileDecorationRules.hiddenStyle(entry.isDirectory())` — so a matching name-pattern rule always
  wins over the built-in hidden style for a hidden entry, and an entry that is neither matched nor
  hidden resolves to nothing. A resolved `hexColor` becomes the `Qt::ForegroundRole` (unset when
  nothing resolves or the resolved rule has no color override); a resolved font override/style bits
  produce a new `Qt::FontRole` value (unset otherwise). Neither role is column-scoped — a match
  styles the whole row across every Details-view column, not just Name.
* **Delegates:** `FileIconDelegate`/`FileTileDelegate`/`SearchResultDelegate` already read
  `Qt::ForegroundRole` off the model for their hand-painted text pen color (§14.21); each gains the
  symmetric `Qt::FontRole` read to `painter->setFont(...)`, with `SearchResultDelegate` layering its
  existing match-highlight bold *on top of* the resolved font rather than replacing it. List/Details
  view modes need no change — Qt's stock delegate already honors both roles.
* **UI:** `SettingsDialog` gains a `General → Colors` tree item (sibling of the existing
  `General → UI` item) and a `FileDecorationsViewModel&` constructor parameter (same posture as its
  existing `HighlightThemeViewModel&`), threaded from `MainWindow` the same way. Its page is a
  `QTableWidget` (Patterns / live style Sample / ↑ / ↓ / Remove, one row per rule) plus "Add Rule".
  Patterns/Sample columns are `Interactive`-resized (user-draggable, with sane default widths) and
  the ↑/↓/Remove columns are `ResizeToContents`-sized `QToolButton`s with standard icons
  (`SP_ArrowUp`/`SP_ArrowDown`/`SP_TrashIcon`) rather than text buttons, so the action columns stay
  compact instead of dominating the row. Reordering and removal call `setRules()` immediately (no
  confirmation — cheap, reversible). The table's first two rows are always the built-in "Hidden
  Files"/"Hidden Folders" rows (a fixed label in the Patterns column instead of pattern text,
  editing their live style Sample the same way as any rule) — they have no ↑/↓/Remove buttons (not
  reorderable relative to pattern rules, not removable) and sit above the pattern-rule rows, which
  render starting at table row 2 keyed by their vector index for Up/Down/Remove. A new
  `FileDecorationRuleDialog` (`src/ui/widgets`) edits one rule: a pattern `QLineEdit` (with a syntax
  hint covering the four worked examples above) — disabled and showing a fixed placeholder instead
  (with the syntax hint hidden) when editing one of the two built-in hidden rows, whose `Kind`
  constructor parameter (`PatternRule`/`HiddenFiles`/`HiddenFolders`) selects this mode and skips
  pattern parsing on accept (`FileDecorationRule::create("", ...)`, always valid); a color
  row whose button *is* the swatch (background = the chosen color, text = its hex code or
  "(Default)", click opens `QColorDialog`) with a "Reset" button to unset it; and a "Font" group box
  replacing the old `QFontDialog` picker (whose Bold/Italic/Underline/Strikeout never actually
  applied — only `family()` was read from it, which confused users seeing effects in that dialog
  that had no effect). The group box's left column stacks an editable `QLineEdit` (a quick-selection
  filter: typing jumps/highlights the first matching row in the list below without altering what was
  typed) above a `QListWidget` of every `QFontDatabase::families()` entry, with no "(Default)" row —
  picking a list row fills the edit field, and the edit field's own (possibly empty) text is the
  single source of truth read on OK, empty meaning "no family override". To its right, a `QListWidget`
  of `QFontDatabase::standardSizes()` (plus the rule's existing size if it's non-standard) serves as
  the size selector, again with no "(Default)" row — no selection means "no size override". Below the
  family column sit the independent Bold/Italic/Underline/Strikeout checkboxes (the single source of
  truth for style bits — no competing dialog-level style control), with a live preview label to their
  right (next to the size list) showing the combined family/size/effects/color. OK validates via
  `FileDecorationRule::create()` and shows an inline error on an unterminated quote rather than
  closing.
* **Not built:** per-folder/per-tab rule scoping (rules are global app-wide, matching every other
  Settings preference in this app); merging multiple matching rules' styles; theme import/export or
  preset packs; a live-regex pattern language beyond the specified `*`/`?`/`"`/`#` mini-glob;
  non-ASCII case folding; decorating synthetic entries ("This PC", drives, quick-access folders).
  No automated coverage for the UI pieces (`SettingsDialog`'s new page, `FileDecorationRuleDialog`,
  `FileListModel`'s new role branches, the delegates' `FontRole` handling) beyond the existing §11
  UI/ViewModel-glue carve-out; the pure `FileNamePattern`/`FileDecorationRule`/`FileDecorationRules`
  logic and `FileDecorationRulesStore`'s round trip are GTest-covered.

### 14.30 Window-Wide Status Bar

A persistent, window-wide bar at the very bottom of the application — outside any pane/tab, unlike
`StatusBarWidget` (§14.27, one instance per pane, item/selection counts + quick-select). Shows,
left-aligned, the focused pane's active tab's drive (`"<label> — <free> free of <total>"`) and,
centered, the machine's computer name (`QSysInfo::machineHostName()`, set once — no Port, not
filesystem-related).

* **Reuses `QMainWindow::statusBar()`** rather than a second independent bottom widget — it is
  already bottom-of-window, full-width, and tab-agnostic. `statusBar()` was previously used only
  for `MainWindow::onStatusMessage`'s transient, 5-second auto-hiding failure messages
  (`showMessage()`), which is unchanged. `QStatusBar` keeps "normal" widgets (`addWidget`, hidden
  while a transient message shows) separate from "permanent" widgets (`addPermanentWidget`, never
  hidden) — the new content is added as a single permanent widget with stretch factor 1
  (`statusBar()->addPermanentWidget(m_mainStatusBarWidget, 1)`), so with nothing else competing for
  space it expands to the whole bar width in the common case, only compressing toward the right
  during the rare seconds a transient message is showing.
* **`MainStatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`/`StatusBarWidget`: an internal `[driveLabel, stretch, computerNameLabel,
  stretch]` `QHBoxLayout` (the second stretch both centers the computer name and reserves room for
  a possible future right-hand section) and one setter, `setDriveInfoText(QString)` — empty string
  blanks it. No `TabViewModel`/use-case dependency.
* **`IFileSystemRepository::driveSpaceInfo(path)`** (new Port method) resolves the drive/volume
  containing `path` (any path on it) to a `DriveSpaceInfo{ displayLabel, freeBytes, totalBytes }`
  (new plain-data type, `src/application/DriveSpaceInfo.h`) — `displayLabel` is fully resolved
  UTF-8 text by the adapter (reusing the same `DriveLabel::driveDisplayLabel` helper and
  `GetDriveTypeW`/`GetVolumeInformationW` calls `StandardFileSystemRepository`'s `buildDriveNode()`
  already makes for "This PC", §14.15), so no OS-specific type crosses the Application boundary —
  same posture as `FileNode::displayName()`. `GetDiskFreeSpaceExW` supplies the byte counts.
  Failure (the "This PC" sentinel, an archive-virtual path, or an unreachable volume) is tolerated
  by simply blanking the label, same as `buildDriveNode()`'s `nullopt` case. `FileNavigationUseCase`
  and `TabViewModel` each add a thin passthrough (`TabViewModel::driveSpaceInfo()` calling
  `m_fileNavigationUseCase.driveSpaceInfo(currentPath())`), the same shape as existing
  single-Port-call wrappers (e.g. `showProperties`).
* **Refresh points:** `MainWindow` tracks the focused pane's active tab the same way it already
  does for sort/search (`m_sortTrackedTab`/`m_searchTrackedTab`) with a new
  `m_driveInfoTrackedTab`, connecting its `currentPathChanged` to re-fetch and reformat the drive
  label. Updates on tab-focus change and on navigation only — same event-driven-only posture as
  §14.27's own refresh, no timer/polling.
* **Threading:** kept synchronous on the UI thread, same as `buildDriveNode()`'s identical Win32
  calls during "This PC" enumeration (§14.15) and consistent with §5's accepted "most Port calls
  are still synchronous" gap.
* **Testing:** `driveSpaceInfo()`'s adapter implementation depends on live, per-machine drive
  state exactly like `buildDriveNode()` — covered by the same §11 carve-out ("live drive/known-
  folder enumeration... not covered by automated adapter tests"). The `FileNavigationUseCase`
  passthrough is covered by an ordinary mock-based test, per §11. `DriveLabel` is unchanged/reused
  as-is.
* **Not built:** live/periodic refresh while idle in the same folder; a populated right-hand
  section (layout reserves room for one); non-Windows implementations (a future Linux backend
  implements `driveSpaceInfo` behind the same Port, matching every other Windows-specific adapter
  call); any change to `StatusBarWidget` or `onStatusMessage`'s transient-message behavior.

### 14.31 Git Status Integration (Details View)

Per-file/folder git working-tree status badges in the Details-view file list, computed via
**libgit2** (the plain C library — not libqgit2, which isn't in vcpkg's registry; libgit2 is
wrapped by our own adapter, same posture as libarchive/libvips/FFmpeg/tree-sitter, so "use a
wrapper, never the library directly" is satisfied the same way it already is for those). One status
per file: `??` Untracked, `M` staged/unstaged (Modified), `A` staged (Added), `D` staged/unstaged
(Deleted), `!!` Ignored — seven `GitFileStatus` values total, each mapped to a small colored badge
icon (purple/blue/yellow/green/red/orange/grey respectively) rendered in a new column appended
after Date Modified.

* **New Port**, `IGitStatusProvider` (`src/application`) — kept separate from
  `IFileSystemRepository` (Interface Segregation, same reasoning that already keeps
  `IContextMenuProvider`/`IMediaDecoder` as their own Ports): `statusForDirectory(directory) ->
  Result<GitDirectoryStatus>`, where `GitDirectoryStatus` (`src/application/GitDirectoryStatus.h`,
  a plain Port-adjacent data type, same posture as `DriveSpaceInfo`) holds an optional repository
  root plus a `path -> GitFileStatus` map for `directory`'s immediate children only. `directory` not
  inside any repository is `Ok({nullopt, {}})`, not an error — same "tolerate by blanking" posture
  §14.30 established for `driveSpaceInfo`. **`GitStatusUseCase`** (new, stateless, depends only on
  `IGitStatusProvider&`) is a thin passthrough, same single-Port-dependency shape as
  `FilePreviewUseCase`.
* **`src/adapters/git`** (new module, `explorer_adapters_git`, mirrors `adapters/archive`):
  `LibGit2Repository` — the RAII wrapper around libgit2 itself (the only place its headers are
  included): `open(startDirectory)` discovers a repository walking upward (`Err` if none found —
  satisfies "in the current folder or a subfolder"), `statusForImmediateChildren(relativeDirectory)`
  runs `git_status_list_new` scoped to that directory via `options.pathspec` (so a large repo isn't
  fully re-walked on every navigation) with `GIT_STATUS_OPT_INCLUDE_UNTRACKED |
  GIT_STATUS_OPT_INCLUDE_IGNORED`, `RECURSE_UNTRACKED_DIRS` left off — an entirely-untracked
  subfolder is reported as itself (a single entry) rather than walked file-by-file, a real perf win
  in large repos. Status-flag → `GitFileStatus` mapping has an explicit priority order for a
  partially-staged file (worktree status wins over index status; see the plan record for the full
  list) since req. "one status per file" rules out showing both. `LibGit2StatusProvider` implements
  the Port on top of this, translating a "no repository found" `Err` into `Ok({nullopt, {}})` rather
  than propagating it, and **rolls a nested entry up onto its immediate-child ancestor directory**
  (the first path segment past `directory`) rather than dropping it — a modified file several levels
  deep stays visible without opening every intermediate folder. A folder that rolls up more than one
  nested change keeps its highest-priority one, via a rollup-specific priority order (its own
  ranking, not the flag-collapsing one above): Conflicted, Untracked, DeletedUnstaged,
  ModifiedUnstaged, DeletedStaged, AddedStaged, ModifiedStaged, Ignored last — Ignored sits at the
  bottom here specifically so an ignored build artifact next to a genuinely modified tracked file in
  the same folder doesn't hide the modification behind an "Ignored" badge on the folder row.
* **Threading** — one dedicated background `QThread` (`GitStatusService` +
  `GitStatusWorker`, `src/adapters/viewmodels`), same "one always-alive worker, not a pool" shape as
  `FilePreviewViewModel`/`WindowsConPtyProcess`; every open tab/pane's status requests serialize
  through it, sidestepping any libgit2 thread-safety question by construction (never touched from
  two threads at once) the same way `SQLiteTagRepository` avoids `SQLITE_BUSY` by serializing DB
  access (§5). Dispatch is `QMetaObject::invokeMethod` with a captured functor across both hops, not
  real Qt signals — same reasoning `FilePreviewWorker.h` documents for avoiding meta-type
  registration for non-default-constructible `Result<T>` payloads. `GitStatusService::requestStatus`
  takes a `QObject* context` and guards its reply-marshaling callback with a `QPointer<QObject>`, so
  a request outliving its requester (e.g. a closed tab) is silently dropped rather than dereferencing
  a dangling `this`.
* **Dispatch point is `TabViewModel`, not `FileListModel`** — `FileListModel` gains no new
  constructor dependency, only a plain `setGitStatuses(...)` slot (storing the map, emitting
  `dataChanged` for the new column), keeping it decoupled from git entirely, the same posture
  `resolveDecoration()` keeps it decoupled from `FileDecorationsViewModel`'s concrete type (§14.29).
  `TabViewModel` gains a `GitStatusService&` constructor dependency (threaded through
  `WorkspacePaneViewModel`/`WorkspaceController` exactly like `TagManagementUseCase&`) and dispatches
  a request on every `directoryContentsChanged` — the single funnel point `loadAndApply` already
  provides for every navigation entry point, including the existing `refresh()` used after in-app
  file operations, so a status refresh after e.g. a paste comes for free with no new wiring. Only the
  tab's primary browsing `FileListModel` is wired this way — the two search-result `FileListModel`
  instances never receive `setGitStatuses()`, which is how quick-search/advanced-search are excluded
  (below) with no extra flag.
* **`FileListModel`**: new `Column::GitStatusColumn`, appended after `DateModifiedColumn` (no
  existing column index shifts). "Details view only" falls out for free from the existing
  `QTreeView`/`QListView` split (§2.3.1) — `QListView`'s icon/list/tile modes already only read
  column 0. `data()` returns `{}` (no badge) for any entry not present in the status map; a hit
  returns a cached 16×16 badge icon (`Qt::DecorationRole`, one per `GitFileStatus` value, built
  once) plus a human-readable `Qt::ToolTipRole` string. Synthetic rows are skipped, same as
  decorations.
* **Not built:** status in quick-search/advanced-search result views (those `FileListModel`
  instances span arbitrary directories, which doesn't fit a per-directory status query — a
  deliberate v1 scope cut); rollup past the immediate-child level (a folder's badge reflects
  changes anywhere in its subtree, but a grandchild folder itself gets no separate entry unless
  `directory` is queried directly for it); live/background refresh outside of navigation (no
  filesystem or `.git`-index watcher — matches
  §14.30's "event-driven only" precedent); rename detection (no "R" status in scope); any stage/
  unstage/commit/other write action — read-only display only. No automated coverage for the
  `FileListModel`/`TabViewModel`/`GitStatusService` glue, per the same §11 UI/ViewModel-glue
  carve-out `FilePreviewViewModel`/`DriveSpaceInfoWorker` already fall under; `GitStatusUseCase` is
  covered by an ordinary mock-based test and `LibGit2StatusProvider` by a real-temp-repo adapter
  test (initialized via libgit2's own `git_repository_init`, not a `git` CLI shell-out), per §11.

### 14.32 Live Directory Update (Visible Tabs, Non-Recursive)

Automatically reflects a file being created/deleted/modified in a browsed folder without the user
pressing Refresh — scoped to the tabs currently **shown** (the active tab of each pane the current
`SplitLayout` makes visible: 1 in Single, 2 in a split, 4 in the grid) and to that folder's
**immediate children only**, never a nested subdirectory.

* **New Port**, `IDirectoryWatcher` (`src/application`) — kept separate from
  `IFileSystemRepository`, same Interface Segregation reasoning as `IGitStatusProvider`/
  `IContextMenuProvider`/`IMediaDecoder`: `watch(directory, std::function<void()> onChanged) ->
  Result<DirectoryWatchId>` (multiple independent watches on the same directory are allowed) /
  `unwatch(DirectoryWatchId)`. A non-real path (the "This PC" sentinel, an archive-virtual path, a
  path that no longer exists) surfaces as a `Result` failure rather than crashing; the caller
  simply doesn't get live updates for that tab (same "tolerate by blanking" posture as
  `driveSpaceInfo`, §14.30). **`DirectoryWatchUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `GitStatusUseCase`).
* **`src/adapters/filesystem`** gains `StandardDirectoryWatcher` (implements the Port on top of
  one owned `QFileSystemWatcher` — chosen over hand-rolling `ReadDirectoryChangesW` because it's
  already non-recursive per directory by construction, and its Windows backend watches
  name/attribute/size/last-write changes of immediate children, covering create/delete/rename/
  modify for free; it also gives a future Linux port (inotify backend) for free) and
  `DirectoryWatchRegistry` (pure C++, no Qt — refcounted path→watcher-callbacks bookkeeping
  deciding when to actually start/stop the native watch — same "pure logic factored out of OS
  glue" split as `AnsiTerminalBuffer`/`WindowsConPtyProcess`, GTest-covered). Path conversion goes
  through `.wstring()` → `QString::fromStdWString(...)`, never `.string()`/`PathUtf8::toUtf8()`
  (§6). This is `adapters/filesystem`'s first Qt dependency (`Qt6::Core`, for
  `QFileSystemWatcher`/`QObject`) — `StandardFileSystemRepository`/`DriveLabel` use plain
  `std::filesystem`/Win32 only.
* **`DirectoryWatchService`** (new, `src/adapters/viewmodels`) — owned long-lived by
  `WorkspaceController` (constructed internally, same posture as `TagListViewModel`/
  `FilePreviewViewModel`/`FileOperationsController`). `setVisibleTabs(tabs)` diffs against the
  currently-watched set: newly-visible tabs are watched *and* refreshed once immediately to catch
  up on anything missed while unwatched (mirrors `FilePreviewViewModel::setPanelActive(true)`'s
  reactivate-and-catch-up posture); no-longer-visible tabs are unwatched. Each watched tab tracks
  its `DirectoryWatchId`, a ~400ms debounce `QTimer` (coalesces a burst of native change events —
  e.g. a multi-file copy — into one refresh), and connections to that tab's `currentPathChanged`
  (re-point the watch at the new directory, same tab stays watched) and `destroyed()` (cleanup on
  tab close). A debounced notification calls the existing `TabViewModel::refresh()` slot (§14.10)
  — no new refresh path. Only the normal-browsing `FileListModel` is affected; a watched tab
  currently showing quick/advanced-search results still refreshes its (hidden) primary listing in
  the background, same scope cut §14.31 already made.
* **`WorkspaceController`** gains a `DirectoryWatchUseCase&` constructor parameter and a private
  `updateLiveWatchedTabs()` (`WorkspaceLayoutTopology::visiblePanes(m_layout)` → each visible
  pane's `activeTab()` → `DirectoryWatchService::setVisibleTabs`), called after construction, from
  `setLayout()`, and from the existing per-pane `activeTabChanged` connection loop — deliberately
  **not** from `focusedPaneChanged`, since visibility (not focus) decides what's watched: all 4
  panes' active tabs live-update under `FourGrid` even though only one pane has focus.
* **Threading** — none new. Registering/unregistering a directory watch is a fast, non-blocking
  syscall, same "cheap OS call stays synchronous on the UI thread" posture as `driveSpaceInfo`
  (§14.30)/`ShellExecuteW` (§14.11); `QFileSystemWatcher` delivers notifications through Qt's own
  event loop with no manual thread. Unlike git-status (a real disk/`.git` scan, needing
  `GitStatusService`'s background thread), there is no scan to offload here.
* **Known limitation:** `refresh()`'s `loadAndApply()` already clears the current selection and
  fully resets `FileListModel` (`beginResetModel()`/`endResetModel()`) on every call — pre-existing
  behavior for the manual Refresh button and the post-file-op refresh (§14.10), now also triggered
  by external changes, more frequently and more disruptively than before (Explorer itself
  preserves selection across a live update). Selection-preserving refresh is a real improvement but
  a separate, larger change to `FileListModel`/`loadAndApply`; deliberately out of scope here.
* **Not built:** watching inactive tabs or hidden-pane tabs (they simply go stale until shown or
  manually refreshed); live updates inside quick-search/advanced-search result views; a Linux
  `IDirectoryWatcher` implementation (deferred until Linux support is actually built, same posture
  as every other Windows-first Port). No automated coverage beyond `DirectoryWatchRegistry`
  (pure bookkeeping) and `DirectoryWatchUseCase` (mocked-Port passthrough) — `WorkspaceController`/
  `DirectoryWatchService`/`StandardDirectoryWatcher` fall under the existing §11 UI/ViewModel-glue
  and OS-shell carve-outs, smoke-tested manually.

### 14.33 Folder Monitoring (Recursive, Per-Tab)

A third right-panel tab, "Monitoring" (§14.24's panel already supports more than one tab with no
further plumbing), that recursively watches one tab's current directory (including every
subdirectory) for create/delete/modify and shows a live, colored event log plus a running total —
primarily meant for watching a long-running build (Specification.md §2.1). Deliberately a separate
Port/adapter from `IDirectoryWatcher`/`StandardDirectoryWatcher` (§14.32) rather than an extension of
it: that watcher is intentionally non-recursive and reports no path or change kind (just "re-list this
folder"), whereas this feature needs full-subtree coverage plus a classified, path-carrying event per
change — `QFileSystemWatcher` (§14.32's backend) cannot deliver either.

* **`src/domain/PathContainment.h`/`.cpp`** (new, pure, no framework deps) — `isPathWithinOrEqual
  (candidate, root)`, a lexical is-subpath check via `path::lexically_relative` (never narrows to
  `std::string`, §6-safe for non-ASCII paths). Backs the navigation guard below; GTest-covered
  (`tests/domain/PathContainmentTest.cpp`).
* **New Port**, `IFolderMonitor` (`src/application`) — `startMonitoring(rootDirectory,
  std::function<void(const std::vector<FolderChangeEvent>&)> onChanged) -> Result<FolderMonitorId>` /
  `stopMonitoring(FolderMonitorId)`. `FolderChangeEvent` (`src/application/FolderChangeEvent.h`, plain
  data, same posture as `DriveSpaceInfo`/`GitDirectoryStatus`) holds a root-relative path, a
  `FolderChangeKind` (`Added`/`Removed`/`Modified`), and a best-effort `isDirectory` flag. Rejects the
  "This PC" sentinel and archive-virtual paths as a `Result` failure, same posture as
  `IDirectoryWatcher::watch`. **`FolderMonitorUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `DirectoryWatchUseCase`).
* **`StandardFolderMonitor`** (`src/adapters/filesystem`, new, Windows-only implementation behind the
  cross-platform-named Port, same "Windows first" posture as `StandardDirectoryWatcher`/
  `ShellContextMenuProvider`): one background `std::thread` per active monitor blocking on
  `ReadDirectoryChangesW` (`bWatchSubtree = TRUE`) — the same "one background thread per resource,
  results marshaled back via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`" shape
  `WindowsConPtyProcess` already established for its ConPTY reader thread. A small in-memory
  path→isDirectory index, seeded by one initial recursive scan at `startMonitoring` and kept current
  as Added/Modified events arrive, lets a Removed event still report `isDirectory` (an already-deleted
  path can't be `stat()`-ed) — an accepted heuristic (falls back to "file" on a miss), same posture as
  the file-identity hash (§8) or `ArchivePathResolver`'s nested-archive limitation. Rename events
  (`FILE_ACTION_RENAMED_OLD_NAME`/`NEW_NAME`) fold into `Removed`/`Added` — no dedicated "renamed"
  kind. The directory handle is opened with `FILE_FLAG_OVERLAPPED`, and each monitor also holds a
  manual-reset stop `HANDLE`: the worker thread waits on `WaitForMultipleObjects` over the pending
  read's `OVERLAPPED` event and the stop event, since closing the handle alone does not reliably
  interrupt a synchronous `ReadDirectoryChangesW` from another thread (a documented Win32 hang
  hazard). `stopMonitoring` signals the stop event, which makes the worker cancel the pending read
  via `CancelIoEx` + a blocking `GetOverlappedResult`, then joins the thread before closing the
  directory handle. Non-Windows builds fail every call with `ErrorCode::IoError`. Falls under the
  existing OS-shell/live-notification carve-out (§11) — no automated adapter test, smoke-tested
  manually, same as `StandardDirectoryWatcher`/`WindowsConPtyProcess`.
* **Per-tab state on `TabViewModel`**, not a separate global monitor — the same "state lives on the
  tab" posture already used for sort/search/advanced-search (§14.12/§14.18/§14.19), so two different
  tabs can independently monitor at once. New `FolderMonitorUseCase&` constructor dependency (threaded
  through `WorkspacePaneViewModel`/`WorkspaceController`/`CompositionRoot` exactly like
  `GitStatusService&`); state = active flag, monitored root, a capped-200 ring-buffer log
  (`std::deque<FolderChangeEvent>`), an uncapped running total, and a pinned flag. `startMonitoring()`/
  `stopMonitoring()` slots; stopping also clears the pinned flag (nothing left to keep pinned) but
  deliberately leaves the log/total as-is until the next `startMonitoring()` clears them for a fresh
  session — so a finished build's final tally stays visible until the user starts watching again.
  Also gains three pure navigation-guard queries: `upTarget()` (the parent-or-ThisPC-sentinel `goUp()`
  already computes internally, now exposed so `goUp()` itself becomes `navigateTo(upTarget())`) and
  `peekBack()`/`peekForward()` (backed by two new non-mutating peek methods on `NavigationHistory`).
* **Navigation/close guard, entirely in `WorkspacePaneWidget`** (never a callback threaded into
  `TabViewModel` — same "confirm in the widget, then call the ViewModel" posture
  `onDeleteRequested`'s `QMessageBox::question` already establishes, §14.10): a shared
  `confirmLeavingMonitoredScope(tab, targetPath)` helper, built on `isPathWithinOrEqual`, gates every
  navigation call site that can move a tab's path — the toolbar Back/Forward/Up actions and
  `FileBrowserView`'s equivalent keyboard signals (consolidated into three shared slots, removing
  previously-duplicated lambdas), double-click/Enter into a folder, and the address bar — stopping
  monitoring and proceeding on "Yes", leaving the tab exactly where it was on anything else. Note this
  generalizes the product requirement's literal "moves to a parent directory" to "navigates outside
  the monitored subtree" (any sibling/unrelated/drive/This-PC target), since a narrower literal
  reading would let the same warning be trivially bypassed via the address bar or Back/Forward.
  Closing a monitored tab (`QTabWidget::tabCloseRequested`) goes through an equivalent confirm-first
  `onTabCloseRequested` slot instead of calling `WorkspacePaneViewModel::closeTab` directly;
  `WorkspacePaneViewModel::closeTab` itself also calls the tab's `stopMonitoring()` right before
  `deleteLater()` as teardown hygiene. `MainWindow::closeEvent` carries an equivalent whole-workspace
  guard: before saving config and accepting the close, it walks every pane
  (`WorkspacePaneId::PaneA..PaneD`, regardless of which are laid out under the current `SplitLayout`)
  and every tab in each, and if any `TabViewModel::isMonitoringActive()`, prompts with the same
  `QMessageBox::question` pattern before allowing the application to exit; declining calls
  `event->ignore()` and leaves the window and all monitoring sessions untouched.
* **`MonitoringPanelViewModel`** (new, `src/adapters/viewmodels`) — the shared ViewModel behind the
  panel, owned by `WorkspaceController` and retargeted from `retargetFocusedTab()` alongside
  `TagListViewModel`/`FilePreviewViewModel`, but with no Port dependency of its own (all real state
  lives on `TabViewModel` above) — purely a retargeting/forwarding shim. Its `setFocusedTab(tab)` is a
  no-op whenever the currently-displayed tab's monitoring is pinned, which is what makes "pin" keep
  the panel locked onto that tab's session regardless of which pane/tab actually has focus
  (Specification.md §2.1) — otherwise it switches and re-emits the new tab's current
  active/log/total/pinned state, the same disconnect-old/connect-new/refresh-now shape
  `TagListViewModel::setActiveTab` already uses.
* **`MonitoringLogModel`** (new, `src/ui/widgets`, mirrors `FolderPreviewListModel`'s lazy-decoration
  posture) — a `QAbstractListModel` over the displayed tab's ring buffer; a generic file-or-folder icon
  keyed only by each event's `isDirectory` flag (never a per-row shell-icon lookup — a deleted entry
  can't be resolved to one anyway, and a fast-scrolling build log shouldn't pay for one per row even
  for entries that still exist) and a foreground color keyed by `FolderChangeKind` (new
  `UiColors::kMonitorAddedGreen`/`kMonitorRemovedRed`/`kMonitorModifiedYellow` constants). Incremental
  `beginInsertRows`/`beginRemoveRows` on append (not a full reset per event) keeps a busy monitor
  cheap. **`MonitoringPanelWidget`** (new, `src/ui/widgets`) — Start/Stop `QPushButton` plus a
  checkable pin `QToolButton` on the same row (enabled only while monitoring is active), a `QListView`
  over `MonitoringLogModel` that calls `scrollToBottom()` on every append, and a `Total: <N>`
  `QLabel` — wired to `MonitoringPanelViewModel`'s signals the same way `TagPanelWidget` wires to
  `TagListViewModel`'s. `MainWindow::createWorkspace()` adds it as a third `RightPanelWidget` tab next
  to "Tags"/"Preview" — no `RightPanelWidget` change needed.
* **Not built:** a Linux `IFolderMonitor` implementation (deferred until Linux support is actually
  built); a dedicated "renamed" event kind/color; monitoring-session persistence across app restart
  or across a Stop→Start cycle beyond "Stop freezes the log/total, Start clears them," matching the
  terminal/bottom-panel's existing unpersisted posture (§14.23); a global one-monitor-at-a-time cap
  (multiple tabs may monitor concurrently by design); real per-file OS shell icons in the log rows;
  monitoring a "This PC" or archive-virtual path
  (rejected, same as `IDirectoryWatcher::watch`); any UI for reviewing/exporting a session's full
  history beyond the live 200-row window plus the running Total. No automated coverage beyond
  `PathContainment` (pure) and `FolderMonitorUseCase` (mocked-Port passthrough) — `TabViewModel`'s new
  state, `NavigationHistory`'s two peek additions aside, `MonitoringPanelViewModel`,
  `MonitoringLogModel`, `MonitoringPanelWidget`, `StandardFolderMonitor`, and the
  `WorkspacePaneWidget` guard wiring fall under the existing §11 UI/ViewModel-glue and OS-shell
  carve-outs, smoke-tested manually.

### 14.34 Git Branch Status and Checkout (Per-Pane Status Bar)

Extends the per-pane status bar (§14.27) with a repository-level git indicator, distinct from
§14.31's per-file Details-view badges: whenever the focused tab's current folder is inside a git
working tree, the status bar's center shows the current branch name (or a short commit id when
`HEAD` is detached) next to a plain count of the repository's changed files; clicking the branch
name opens a menu of local branches and checks out the one the user picks.

* **New Port**, `IGitBranchProvider` (`src/application`) — kept separate from `IGitStatusProvider`
  (Interface Segregation, same split reasoning as `IFolderMonitor`/`IDirectoryWatcher`): that Port's
  callers are all read-only per-file badge consumers, and have no business depending on a Port that
  also exposes a write operation (`checkoutBranch`). Three methods: `repositorySummary(directory) ->
  Result<GitRepositorySummary>` (tolerate-by-blanking when `directory` isn't inside a repository,
  same posture as `IGitStatusProvider::statusForDirectory`), `listLocalBranches(directory) ->
  Result<vector<string>>` (local branches only, no remotes), and `checkoutBranch(directory,
  branchName) -> Result<void>` (a **safe**, non-forced checkout — fails rather than discarding
  changes if it would overwrite a modified tracked file, matching plain `git checkout`'s default
  safety; no `--force` path exists). `GitRepositorySummary` (`src/application/
  GitRepositorySummary.h`, plain Port-adjacent data, same posture as `GitDirectoryStatus`) holds an
  optional `repositoryRoot`, a `headDisplayName` (branch short name, or a short commit id when
  detached, or the would-be branch name pre-first-commit on an unborn branch), an `isDetached` flag,
  and a repo-wide `changedFileCount`. **`GitBranchUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `GitStatusUseCase`).
* **`LibGit2Repository` gains four methods** (`src/adapters/git`, the same RAII wrapper §14.31
  introduced): `headDisplayName(bool& isDetachedOut)` (`git_repository_head` for the normal case;
  `GIT_EUNBORNBRANCH` falls back to reading raw `HEAD`'s symbolic target via `git_reference_lookup
  (..., "HEAD")` so a freshly-`git init`'d repo with no commits still shows its would-be branch
  name, matching plain `git status`'s pre-first-commit wording); `changedFileCount()` (reuses
  `statusForImmediateChildren("")` at the workdir root — already generically "whole repo" when given
  an empty pathspec — filtering out `Ignored` and counting the rest, so an entirely-untracked
  directory still costs one entry rather than a per-file walk, the same trade-off that method's
  existing doc comment already accepts); `listLocalBranches()` (`git_branch_iterator_new
  (GIT_BRANCH_LOCAL)`); `checkoutBranch(branchName)` (`git_branch_lookup` + `git_reference_peel` to
  the target commit + `git_checkout_tree` with `GIT_CHECKOUT_SAFE` — never `FORCE` — + `git_
  repository_set_head` to `refs/heads/<branchName>`; any libgit2 failure, including a safe-checkout
  conflict, is surfaced as `Result::failure` with HEAD/the working tree left untouched).
  **`LibGit2BranchProvider`** (new, implements `IGitBranchProvider`) opens a fresh `LibGit2Repository`
  per call exactly like `LibGit2StatusProvider`, no repository/connection cache.
* **Reuses the existing single-worker-thread posture, not a second thread** — `GitStatusService`/
  `GitStatusWorker` (§14.31) gain a `GitBranchUseCase&` dependency alongside their existing
  `GitStatusUseCase&` and three new request methods (`requestRepositorySummary`,
  `requestLocalBranches`, `requestCheckout`, same `QObject* context` + `QPointer`-guarded-callback
  shape as `requestStatus`), all dispatched onto the *same* `m_workerThread` `requestStatus` already
  uses — deliberately not a second dedicated thread, since the entire point of serializing through
  one worker was "sidestepping any libgit2 thread-safety question by construction" (§14.31), and
  that guarantee is strongest when every libgit2 call in the process, badges or branch/checkout,
  funnels through the one thread.
* **Dispatch and caching on `TabViewModel`, not `FileListModel`** — same funnel point §14.31
  established: the `directoryContentsChanged` handler that already calls `requestGitStatus(path)`
  now also calls a new `requestGitRepositorySummary(path)`, so a repo-wide summary refresh comes for
  free on every navigation and on `refresh()` (including the refresh a completed checkout below
  triggers) with no new wiring. The latest summary is cached on the tab
  (`lastGitRepositorySummary()`) so switching tabs shows the right branch/count immediately from
  cache, the same "read the cached value immediately, let the async refresh correct it later" shape
  `refreshStatusCounts()` already uses for object counts. New signals: `gitRepositorySummaryChanged
  (const GitRepositorySummary&)` and `gitOperationFailed(const QString&)`. Two new public methods:
  `requestLocalBranches(callback)` (a thin forward to `GitStatusService::requestLocalBranches` scoped
  to the cached `repositoryRoot`, no-op returning an empty list when the tab isn't in a repository)
  and `checkoutBranch(branchName)` (forwards to `GitStatusService::requestCheckout`; a no-op if
  `branchName` is already the current, non-detached `HEAD`; on success calls the tab's own
  `refresh()`, which re-navigates the current folder and — via the funnel above — re-fetches both the
  file list and the git status/summary for the new `HEAD`; on failure emits `gitOperationFailed`
  with the `Result`'s error message, following the same `QString::fromStdString(result.error()
  .message)` convention every other ViewModel failure signal already uses).
* **`StatusBarWidget`** (`src/ui/widgets`, §14.27) gains a small `QToolButton` (branch name, hidden
  when the tab isn't in a repository) immediately followed by a plain, non-clickable `QLabel`
  showing only `changedFileCount`, inserted between the existing left-aligned counts label and the
  stretch before the quick-select field so the pair renders centered in the bar. New members: slot
  `setGitInfo(bool inRepository, const QString& headDisplayName, int changedFileCount)`; signal
  `branchButtonClicked()` (emitted on click, no menu attached yet — same "plain display widget,
  no ViewModel dependency, `WorkspacePaneWidget` mediates" posture as `AddressBarWidget`'s
  `folderSuggestionsRequested`, §14.20); method `showBranchMenu(const QStringList& branchNames,
  const QString& currentBranchName)` (builds and pops up a `QMenu` at the button's position, one
  checkable `QAction` per branch — checked for `currentBranchName` — and re-emits the chosen one via
  signal `branchSelected(const QString&)`). `WorkspacePaneWidget::bindToolBarToTab` additionally
  (dis)connects the newly-bound tab's `gitRepositorySummaryChanged` to a `updateGitStatusBar(const
  GitRepositorySummary&)` slot (which calls `m_statusBar->setGitInfo(...)`, applying the cached
  value immediately on bind, same as the counts refresh) and `gitOperationFailed` to a new
  `WorkspacePaneWidget::gitOperationFailed(const QString&)` signal, wired in `MainWindow` alongside
  the existing per-pane `navigationFailed` → `onStatusMessage` connections (§14.14's error-surfacing
  convention). `WorkspacePaneWidget` itself wires `m_statusBar`'s `branchButtonClicked` to call
  `m_boundTab->requestLocalBranches(...)` and, on the async result, `m_statusBar->showBranchMenu
  (...)`; and `branchSelected` to `m_boundTab->checkoutBranch(branchName)`.
* **Not built:** remote branches in the menu (local only, matching the product ask); creating a new
  branch or any other write action besides checkout; a confirmation dialog before checkout (the menu
  click itself is the deliberate action, and a conflicting checkout already fails safely without
  touching the working tree); force-checkout/discard-changes recovery UI when a checkout is refused;
  live/background refresh of the branch indicator outside of navigation (matches §14.31/§14.30's
  "event-driven only" precedent — a checkout performed outside the app, e.g. from a terminal, isn't
  picked up until the next navigation or manual refresh); remembering which branch was checked out
  across restarts; any UI for an unborn/headless repository beyond showing its would-be branch name.
  No automated coverage for the `TabViewModel`/`StatusBarWidget`/`WorkspacePaneWidget` glue (§11's
  existing UI/ViewModel-glue carve-out); `GitBranchUseCase` gets an ordinary mocked-Port test and
  `LibGit2Repository`'s four new methods / `LibGit2BranchProvider` get a real-temp-repo adapter test
  (via libgit2's own `git_repository_init`/`git_branch_create`, not a `git` CLI shell-out), per §11,
  including a checkout-refused-by-conflicting-changes case.

### 14.35 Favorites

User-curated shortcuts to files/folders, organized into optional categories, managed from a new
`Favorites` top-level menu (Specification.md §2.1). Like §14.29's per-file/folder color rules or
§14.26's highlight theme, this is a persisted UI preference — **no Domain or Application layer
change**, same posture as every other entry in this section.

* **`src/adapters/favorites`** (new module, `explorer_adapters_favorites`, mirrors
  `adapters/decoration`, links only `explorer_domain`): `FavoriteItem` (`Id` = `std::int64_t`, mirrors
  `Tag::Id`; `path`, display text derived from `path.filename()` at render time, no stored display
  name) and `FavoriteCategory` (`Id`, `name`, an ordered `std::vector<FavoriteItem>`). `Favorites`
  owns one ordered top-level `std::vector<std::variant<FavoriteItem, FavoriteCategory>>` plus an
  internal monotonic id counter, with a pure mutation API (add/remove/rename/reorder/move-between-
  categories) — no Qt, no I/O. Deleting a category flattens its items back to top-level rather than
  deleting them (non-destructive by default, matching this app's no-undo-in-v1 posture,
  Specification.md §5). Adding an already-favorited path is a no-op (dedup by path).
* **`src/adapters/config`** gains `FavoritesStore` (tolerant `load()`/atomic `save()` via the
  existing `JsonFileStore` helpers, own `favorites.json`, §9), `FavoritesSaveWorker`, and
  `FavoritesViewModel` — the same three-piece shape §14.29 established for
  `FileDecorationRulesStore`/`FileDecorationRulesSaveWorker`/`FileDecorationsViewModel`, including why
  these live in `adapters/config` rather than `adapters/viewmodels`: `adapters/config` already depends
  on `adapters/viewmodels`, so the reverse edge would be a build-graph cycle. A mutation applies to
  the live `Favorites` synchronously, emits `favoritesChanged()`, and queues the actual disk save onto
  the worker thread (Architecture.md §5). `CompositionRoot` owns the usual store→model→viewmodel
  triple and exposes `favoritesViewModel()`.
* **Selecting an entry after navigation (new capability)** — nothing previously let a caller navigate
  a tab *and* select a specific resulting row; the only prior use of
  `FileBrowserView::selectEntryByPath` (now made public) was Details-view arrow-key navigation, called
  synchronously right after its triggering signal since navigation is still synchronous on the UI
  thread (§5). New `WorkspacePaneWidget::navigateAndSelect(directory, optional entryToSelect)` routes
  through the same `confirmLeavingMonitoredScope` guard every other navigation entry point already
  uses, navigates, then selects if requested and the navigation landed.
* **Menu wiring, `MainWindow`** — fleshes out the previously-empty `Favorites` menu stub: "Add to
  Favorites" (targets the focused tab's selection, or its current browsed folder if nothing is
  selected — same fallback §14.9's tag panel already established; disabled with no focused tab or on
  the `VirtualPaths::ThisPC` sentinel), "Manage Favorites..." (opens `ManageFavoritesDialog`, same
  `exec()`-a-modal shape as `TagManagerDialog`), a separator, then the live entries — rebuilt lazily
  on the menu's `aboutToShow` (a plain `QAction` per uncategorized item, a `QMenu` submenu per
  category populated the same way) rather than kept continuously in sync, so no extra
  `favoritesChanged()` wiring is needed in `MainWindow` itself. Clicking a leaf resolves file-vs-folder
  via the existing `FileNavigationUseCase::stat()` passthrough: a folder navigates the focused pane's
  active tab into it; a file navigates to `path.parent_path()` and selects the file there (product
  decision — folder favorites behave like Explorer Quick Access, not "reveal in parent").
* **`ManageFavoritesDialog`** (new, `src/ui/widgets`) — a `QTreeWidget`-based dialog wired directly to
  `FavoritesViewModel` (no separate dialog-specific ViewModel needed), mirroring `TagManagerDialog`'s
  thin-dialog-over-a-ViewModel shape but using a two-level tree (category → items, native
  expand/collapse covering the "collapsible sections" requirement) instead of `TagManagerDialog`'s
  flat `FlowLayout` of chips. Toolbar: Add File.../Add Folder... (`QFileDialog`, since Qt has no
  single native picker for "either a file or a folder"), Add Category, Rename (category rows),
  Move to Category... (item rows), Move Up/Move Down (enabled by sibling position at the row's own
  level, same boundary-check shape `SettingsDialog::refreshDecorationRows()` already uses for its
  decoration-rule up/down buttons), Remove, Close. Reordering is button-based, not drag-and-drop —
  matching this codebase's existing ordered-list-editing precedent (§14.29) rather than introducing a
  new interaction pattern. Rebuilds the whole tree on `favoritesChanged()`, same "re-render on any
  change" posture `TagManagerDialog::rebuildTagChips` already uses.
* **Not built:** drag-and-drop add (from the file browser onto the Favorites menu) or a right-click
  "Add to Favorites" context-menu entry — menu-only entry point in this pass; drag-and-drop reordering
  in the Manage dialog; a keyboard shortcut for "Add to Favorites"; broken-favorite detection/
  indicator or any periodic re-check of a favorited path's existence; import/export of the favorites
  list; nested categories (one flat level, matching the "collapsible sections" requirement, not a
  category tree). No automated coverage for `FavoritesViewModel`/`ManageFavoritesDialog`/the
  `MainWindow`/`WorkspacePaneWidget` menu and navigate-and-select glue, per the existing §11
  UI/ViewModel-glue carve-out (same as `FileDecorationsViewModel`/`TagManagerViewModel`/
  `TagManagerDialog`) — smoke-tested manually instead. `Favorites`' pure mutation logic and
  `FavoritesStore`'s JSON round-trip/tolerant-load behavior each get an ordinary GTest suite under
  `tests/adapters`, mirroring `FileDecorationRulesTest.cpp`/`FileDecorationRulesStoreTest.cpp`.
