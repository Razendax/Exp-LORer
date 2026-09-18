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

**Ports:**
* `IFileSystemRepository` — file operations: list, recursive list, move, copy, delete, hash,
  single-path stat, create folder/file, open-with-default-app, show OS properties dialog.
* `ITagRepository` — persists tags and file/tag associations.
* `IMediaDecoder` — decodes media streams, extracts thumbnails.
* `IContextMenuProvider` — builds a registry/COM-sourced context menu as plain data
  (`ContextMenuEntry`) and invokes the chosen entry — it hands back data, it doesn't show the OS's
  own popup (§14.13).

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
* **Media** (`adapters/media`) — `QtMediaDecoder`/`FFmpegMediaDecoder`, implements `IMediaDecoder`;
  not yet implemented.
* **Logging** (`adapters/logging`) — `Logging`, a thin wrapper over spdlog (rotating file sink,
  console sink in debug builds); the only place spdlog is referenced (§6, §14.22).

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
| UI (Widgets) | `MainWindow`, `WorkspaceLayoutWidget`, `WorkspacePaneWidget`, `FileBrowserView`, `TagPanelWidget`, `ContextMenuBuilder`, dialogs |
| UI (QML) | Media viewer (not yet implemented) |

## 5. Concurrency and Threading Model

* **Main (UI) thread:** Qt event loop and ViewModels only — no direct disk/DB I/O.
* **Design intent:** Application-layer Port calls (file scanning, copying, DB queries, media
  decoding) dispatch to background threads; results marshal back to ViewModels via queued Qt
  signals/slots.
* **Current state:** most Port calls (navigation, file ops, context menu, recursive search) are
  still synchronous on the UI thread — an accepted, tracked gap against the design intent above,
  not a regression. Background dispatch is deferred throughout §14.
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
  spdlog, GoogleTest/GoogleMock, xxHash), consumed via `find_package`; `CMakePresets.json` pins the
  vcpkg toolchain.
* **Logging:** `spdlog`, wrapped behind a thin `Logging::log(...)` call site so it isn't referenced
  outside one adapter header.
* **Dependency injection:** no framework — a single composition root
  (`src/app/CompositionRoot.cpp`) constructs concrete adapters and injects them via constructor
  injection. Use cases/ViewModels only ever depend on Application-layer interfaces.
* **Paths:** carried as `std::filesystem::path` through Domain/Application/Adapters, never raw
  `std::string`. Windows long paths (>260 chars) go through the `\\?\` prefix inside
  `StandardFileSystemRepository`; conversion to UTF-8 happens only at the SQLite boundary
  (`SQLiteTagRepository`), since SQLite stores/compares `TEXT` as UTF-8.
* **Single-instance enforcement:** a named mutex (`CreateMutexW`, Windows) / `flock`'d PID file
  (Linux) per user session. A second launch forwards its startup args to the running instance over
  local IPC (e.g. `QLocalSocket`) and exits. Exists to stop two processes writing to `explorer.db`/
  the thumbnail cache concurrently (§5, §8).

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
* **Thumbnail cache:** on disk under the app-local cache directory, keyed by the same hash; entries
  store the thumbnail plus source `size`/`modified_at` so stale ones regenerate automatically. An
  in-memory LRU sits in front for the visible viewport.
* **Cache eviction:** capped by total size (default 512MB); least-recently-accessed entries pruned
  on startup or when exceeded.

## 9. Application Data Locations & Configuration

* **Locations:** database, logs, and thumbnail cache live under `%LOCALAPPDATA%/Exp-LORer/`
  (Windows), via `QStandardPaths` so the Linux path (`~/.local/share`, `~/.cache`) needs no code
  change. Logs specifically live in a `logs/` subfolder there (§14.22), written via
  `Logging` (`adapters/logging`).
* **Session/settings:** window geometry and the split-window session (layout, each pane's tabs —
  path/view mode/sort order — focused pane) persist to `config.json` (§14.14), written by
  `AppConfigStore`, same directory as `explorer.db`. `QSettings` remains in use only for the
  context-menu shell-extensions toggle (§14.13), not yet migrated; bookmarks and thumbnail cache
  size limit are unimplemented in either mechanism.
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
│   │   ├── media/       # QtMediaDecoder / FFmpegMediaDecoder (not yet implemented)
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
  window appears; it is not a substitute for the CTest-based coverage above.
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
regardless of the tab's own `ViewMode`. Mutually exclusive with quick search on the same tab; each
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

`FileListModel` is the single filter/gray point rather than the Application layer or
`TabViewModel`: it already backs normal browsing, §14.18 quick search, and §14.19 advanced search
alike (one instance each, all fed via `setEntries()`), so implementing it once there covers all
three. It keeps a `m_visibleRows` index over its full `m_entries`, rebuilt whenever entries/sort/the
toggle change; with the toggle on, every row stays visible and `data()` returns a dimmed
`Qt::ForegroundRole` brush for hidden rows (`QGuiApplication::palette()`'s `Disabled`/`Text` color,
so it already tracks the light/dark palette); with it off, hidden rows are dropped from the index
entirely. Qt's stock `QStyledItemDelegate` (List/Details views) honors `Qt::ForegroundRole`
automatically; the three custom delegates that hand-paint text (`FileIconDelegate`,
`FileTileDelegate`, `SearchResultDelegate`) each read the same role for their non-selected pen color
instead of the hardcoded `option.palette.text()` they used before, so all seven view modes plus the
advanced-search results view render consistently.

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
