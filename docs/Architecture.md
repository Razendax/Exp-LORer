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
  `FilePreview` for the right panel's Preview tab (§14.26): folder listing (directories first),
  capped text-prefix read, or a decoded thumbnail via `IMediaDecoder`. Stateless, depends only on
  `IFileSystemRepository`/`IMediaDecoder` — never on another use case, same posture as the rest of
  this layer.
* `GitStatusUseCase` — thin stateless passthrough to `IGitStatusProvider`, backing the Details-view
  git status column (§14.32).
* `FileTransferUseCase` — plans (recursive enumerate + total count/size) and executes a multi-file
  copy or move with per-file progress and cooperative cancellation, backing the custom copy/move
  progress notification (§14.47). Stateless, depends only on `IFileSystemRepository`, same posture
  as `FilePreviewUseCase`.

**Ports:**
* `IFileSystemRepository` — file operations: list, recursive list, move, copy, delete, hash,
  single-path stat, create folder/file, open-with-default-app, show OS properties dialog, read a
  capped byte prefix of a file (`readFilePrefix`, §14.26), extract archive content
  (`extractArchive`, §14.29), and resolve a path to a real, on-disk file for callers that need
  actual file I/O (`materializeForReading` — identity for a real path, a private temp-extracted
  copy for an entry inside an archive, §14.29). `copyFileWithProgress` (§14.47) byte-copies one real
  file with a periodic progress callback that doubles as a cooperative-cancel hook, for
  `FileTransferUseCase`'s per-file execution step — `copy()` above stays a one-shot, non-cancellable
  call for every other existing caller.
* `ITagRepository` — persists tags and file/tag associations.
* `IMediaDecoder` — decodes media streams, extracts thumbnails.
* `IContextMenuProvider` — builds a registry/COM-sourced context menu as plain data
  (`ContextMenuEntry`) and invokes the chosen entry — it hands back data, it doesn't show the OS's
  own popup (§14.13).
* `IGitStatusProvider` — resolves a directory's working-tree git status (per-immediate-child
  `GitFileStatus`, or an empty result when the directory isn't inside a repository), §14.32; also
  `ignoredPaths(root)`, every ignored path under a subtree (files and wholly-ignored directories each
  as one entry), used to prune git-ignored content from a recursive quick search (§14.45).
* `IDirectoryWatcher` — non-recursive immediate-children change notification backing live directory
  update (§14.33).
* `IFolderMonitor` — recursive, subtree-wide change notification with per-event
  add/remove/modify classification, backing per-tab folder monitoring (§14.34). Kept separate from
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
  `FileOperationsController` (clipboard/delete/open/context-menu orchestration, §14.10; also drives
  `FileTransferService` for the custom copy/move progress pipeline, §14.47),
  `FileTransferService`/`FileTransferWorker` (one dedicated background `QThread` executing
  `FileTransferUseCase`, same shape as `AdvancedSearchService`/`AdvancedSearchWorker`, §14.47),
  `FolderTreeModel` (the left panel's lazily-listed folders-only tree, retargeted to the focused
  tab, §14.41),
  `TagManagerViewModel` (global tag CRUD, §14.17).
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
  `IMediaDecoder` around it) sit in front so callers never see cache logic. §14.26.
* **Logging** (`adapters/logging`) — `Logging`, a thin wrapper over spdlog (rotating file sink,
  console sink in debug builds); the only place spdlog is referenced (§6, §14.23).
* **Git** (`adapters/git`) — `LibGit2Repository` (RAII wrapper over libgit2, the only place libgit2
  is referenced), `LibGit2StatusProvider`, implements `IGitStatusProvider` (§14.32).

### 2.3.1 View Modes

Seven Explorer-style view modes (Extra Large/Large/Medium/Small Icons, List, Details, Tiles) share
one `FileListModel`. A `QStackedWidget` (`FileBrowserView`) switches between a `QListView` (icon/
list/tiles) and a `QTreeView` (details, with native column-header sorting). Tiles and the four
icon-size modes use custom `QStyledItemDelegate`s (`FileTileDelegate`, `FileIconDelegate`); List
uses Qt's stock delegate. Icons come from `QFileIconProvider` (real OS shell icons). View mode is
per-tab presentation state, not a Domain/Application concern.

### 2.4 Frameworks & Drivers Layer

* **UI:** Qt 6 Widgets throughout, including the inline video player (§14.67) -- no QML/
  `QQuickWidget` surface exists in the project; the originally-sketched full-window QML image/
  video viewer was never built and is superseded by the Widgets-based player.
* **Database:** SQLite.
* **Media:** FFmpeg (thumbnail/poster-frame decode, `FFmpegMediaDecoder`) and Qt Multimedia
  (`QMediaPlayer`/`QVideoWidget`/`QAudioOutput`, inline playback, §14.67).
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
| Application | `FileNavigationUseCase`, `TagManagementUseCase`, `MediaProcessingUseCase`, `DiskUsageUseCase` (§14.40), `FileTransferUseCase` (§14.47), `DuplicateFinderUseCase` (§14.51), `FragmentationUseCase` (§14.52), `BulkRenameUseCase` (§14.55), and their Ports (§2.2) |
| Adapters | `SQLiteTagRepository`, `StandardFileSystemRepository`, `ShellContextMenuProvider`, `AppConfigStore`, `WorkspaceStore` (§14.60), `WindowsDiskUsageScanner`/`SQLiteDiskUsageRepository` (§14.40), `StandardDuplicateFileScanner` (§14.51), `WindowsFragmentationScanner` (§14.52), plus the ViewModels listed in §2.3 |
| UI (Widgets) | `MainWindow`, `WorkspaceLayoutWidget`, `WorkspacePaneWidget`, `FileBrowserView`, `SidePanelWidget` (left/right panels), `FolderTreeView`, `TagPanelWidget`, `ContextMenuBuilder`/`FileItemContextMenu`, `WorkspaceManagerDialog` (§14.60), `VideoPlayerWidget` (reusable inline video player, first used in the Preview tab, §14.67), dialogs |

## 5. Concurrency and Threading Model

* **Main (UI) thread:** Qt event loop and ViewModels only — no direct disk/DB I/O.
* **Design intent:** Application-layer Port calls (file scanning, copying, DB queries, media
  decoding) dispatch to background threads; results marshal back to ViewModels via queued Qt
  signals/slots.
* **Current state:** most Port calls (navigation, file ops, context menu, quick search §14.18) are
  still synchronous on the UI thread — an accepted, tracked gap against the design intent above,
  not a regression. Background dispatch is deferred throughout §14, with these exceptions:
  `FilePreviewViewModel` (§14.26) dispatches `FilePreviewUseCase` calls to a dedicated worker
  thread, since preview generation (image/video decode) is the first Port-adjacent call slow
  enough, and frequent enough, to need it; and Advanced Search (§14.19/§14.20, async since §14.38)
  dispatches its recursive scan and tag-first candidate fetch to `AdvancedSearchService`/
  `AdvancedSearchWorker`, the same worker-thread shape. In both cases, "cancelling" an in-flight
  request means discarding a stale result by a generation-id check when it arrives, not aborting
  the underlying call itself — neither the libvips/FFmpeg decode nor a `std::filesystem`
  recursive walk/`stat()` loop is interruptible mid-call. Custom copy/move (§14.47) dispatches to a
  third such worker thread, `FileTransferService`/`FileTransferWorker`, but unlike the two above its
  cancellation is a genuine mid-call abort: `IFileSystemRepository::copyFileWithProgress`'s chunked
  copy loop checks a shared `std::atomic<bool>` cancel flag between chunks (Windows:
  `CopyFileExW`'s own progress-callback return value; portable fallback: between buffered-read
  iterations), so pressing Cancel on an in-flight transfer stops it within one chunk, not just at
  the next request boundary.
* **Database concurrency:** `SQLiteTagRepository` wraps a `QSqlDatabase`/`QSQLITE` connection, which
  Qt requires to be used only from the thread that created it — so cross-thread tag-DB access isn't
  achieved by sharing one `TagManagementUseCase`/`SQLiteTagRepository` instance across threads, but
  by giving each thread that needs one its **own** dedicated connection to the same `explorer.db`
  file. `AdvancedSearchWorker` (§14.38) is the first such second connection; `SQLiteTagRepository`
  sets `PRAGMA busy_timeout` so concurrent readers/writers across connections retry briefly instead
  of failing immediately with `SQLITE_BUSY`. This covers in-process concurrency only —
  cross-process concurrency is prevented separately by single-instance enforcement (§6).

## 6. Technology, Language & Build Decisions

* **C++20**; MSVC v143+ (Windows), GCC 12+/Clang 15+ (Linux).
* **Windows first**; all OS-specific code goes through `IFileSystemRepository` so a Linux backend
  can be added without touching Domain/Application.
* **Qt 6** (Widgets + Quick/QML hybrid, §2.4).
* **vcpkg** manifest mode (`vcpkg.json`) for all third-party dependencies (Qt6, SQLite3, FFmpeg,
  spdlog, GoogleTest/GoogleMock, xxHash, libarchive per §14.29, libgit2 per §14.32), consumed via
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
* **Single-instance enforcement, scoped per workspace (§14.60):** a named mutex (`CreateMutexW`,
  Windows; Linux not yet implemented) plus a `QLocalServer`/`QLocalSocket` activation channel,
  implemented in `WorkspaceInstanceGuard` (`src/app/`). The name for both is derived from a hash of
  the resolved app-data directory (§9) **and** the workspace id — not a fixed name, and not just the
  data directory alone — so two instances started against different `--data-dir` overrides (e.g.
  parallel `ui_tests/` runs, or a test run alongside the developer's real running instance) never
  collide with each other, and, within one data directory, two different *workspaces* never collide
  with each other either. A second launch targeting a workspace id that's already running loses the
  mutex, forwards an "activate" message to the existing instance over the matching `QLocalSocket`
  (which raises/focuses that instance's window), and exits before constructing `CompositionRoot` or
  any window. Exists to stop two processes writing the same workspace's session file concurrently
  (§9) — shared resources that remain genuinely shared across every workspace in one data directory
  (`explorer.db`, the thumbnail cache, `favorites.json`, etc.) are not covered by this guard and rely
  instead on `PRAGMA busy_timeout` (§5) and atomic JSON writes (§9) to tolerate concurrent processes.
* **vcpkg overlay ports:** a repo-local `vcpkg-overlays/ports/` directory, registered via a root
  `vcpkg-configuration.json`, holds small custom ports for third-party pieces the public vcpkg
  registry doesn't package (e.g. individual tree-sitter grammars, §14.27) — each is a normal vcpkg
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

CREATE TABLE filesystem_index (
    folder_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id     INTEGER REFERENCES filesystem_index(folder_id) ON DELETE CASCADE,
    path          TEXT NOT NULL UNIQUE,
    name          TEXT NOT NULL,
    size          INTEGER NOT NULL,
    is_directory  INTEGER NOT NULL,
    scanned_at    INTEGER NOT NULL
);
CREATE INDEX idx_filesystem_index_parent ON filesystem_index(parent_id);

CREATE TABLE file_fragmentation (
    path            TEXT PRIMARY KEY,
    size            INTEGER NOT NULL,
    fragment_count  INTEGER NOT NULL,
    scanned_at      INTEGER NOT NULL
);
```

* **`filesystem_index`** backs Disk Usage / Folder Size (§14.40) — one row per scanned file/folder,
  `size` already the recursive aggregate for a directory. Written/read by `SQLiteDiskUsageRepository`
  over its own connection, separate from `SQLiteTagRepository`'s. **Not** gated by the
  `PRAGMA user_version` check above — that counter belongs to the Tags/Files/FileTags schema, owned
  by a different repository class/connection; this table's migration is a standalone
  `CREATE TABLE IF NOT EXISTS`, sufficient since it has no evolving schema yet (§14.40).

* **`file_fragmentation`** backs the Fragmentation bottom-panel tab (§14.52) — one row per scanned
  file, `fragment_count` the number of NTFS extents the file is split into. Written/read by
  `SQLiteFragmentationRepository` over its own connection, separate from `SQLiteTagRepository`'s and
  `SQLiteDiskUsageRepository`'s. Like `filesystem_index`, **not** gated by `PRAGMA user_version`; a
  standalone `CREATE TABLE IF NOT EXISTS`. Unlike `filesystem_index`, stores files only — never a row
  for a scanned folder/drive root itself — so a "files under this folder" lookup matches rows strictly
  *under* a path via a LIKE-prefix-with-escaped-wildcards query, the same idiom
  `SQLiteDiskUsageRepository::saveScan`'s own stale-row cleanup already established (§14.39).

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
  `adapters/media`, §14.26), entries keyed by `xxHash64(path, size, modified_at, requested width,
  requested height)` rather than the file-identity hash above — a changed source file naturally
  produces a different cache filename, so no separate staleness bookkeeping is needed; the
  identity hash above is a rename/move-detection heuristic for tagging (§7), a different problem,
  and isn't involved here. An in-memory LRU sits in front for the visible viewport. Shared by the
  Preview tab's image/video thumbnails (§14.26) and, later, grid-view icons.
* **Cache eviction:** capped by total size (default 512MB); least-recently-accessed entries pruned
  on startup or when exceeded.

## 9. Application Data Locations & Configuration

* **Locations:** database, logs, and thumbnail cache live under `%LOCALAPPDATA%/Exp-LORer/`
  (Windows), via `QStandardPaths` so the Linux path (`~/.local/share`, `~/.cache`) needs no code
  change. Logs specifically live in a `logs/` subfolder there (§14.23), written via
  `Logging` (`adapters/logging`).
* **`--data-dir <path>` startup override:** `main.cpp` resolves the effective app-data directory
  once, before constructing `Logging` or `CompositionRoot`, checking for a `--data-dir` argument
  before falling back to `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)`.
  `CompositionRoot`'s constructor takes the resolved directory and derives `explorer.db`,
  `config.json`, `cache/thumbnails/`, `highlight_theme.json`, `file_decorations.json`,
  `favorites.json`, `notification_settings.json` (§14.37), and `environment_variables.json`
  (§14.57) from it, instead of each looking it up independently. Exists so `ui_tests/` (§11)
  can point each test run
  at an isolated, disposable directory rather than the developer's real session. On Windows the
  argument is read from the process's own wide command line (`GetCommandLineW()` +
  `CommandLineToArgvW()`), not the narrow `argv` `main()` receives — the same ANSI-code-page
  narrowing trap `PathUtf8`/`path::string()` guidance (§6) warns about applies here too, since the
  override value is itself a filesystem path.
* **Session/settings, as named workspaces (§14.60):** window geometry and the split-window session
  (layout, each pane's tabs — path/view mode/sort order — focused pane) persist per *workspace* under
  `workspaces/` rather than to one fixed `config.json`: `workspaces/index.json` holds the
  `{id, name}` registry (owned by `WorkspaceStore`, written only on create/rename/delete), and
  `workspaces/<id>.json` holds that workspace's own session, in the same shape §14.14 already
  defined and written by the same `AppConfigStore` (now constructed once per process, pointed at the
  current workspace's file). A pre-existing single `config.json` is migrated once into
  `workspaces/default.json` (id `"default"`, name `"Default"`) the first time `workspaces/index.json`
  doesn't yet exist. `QSettings` remains in use only for the context-menu shell-extensions toggle
  (§14.13), not yet migrated; the thumbnail cache size limit is unimplemented in either mechanism.
  Favorites persist separately to their own `favorites.json` (§14.36), same directory (shared across
  every workspace, not per-workspace), same tolerant-load/atomic-save shape as
  `highlight_theme.json`/`file_decorations.json`.
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
│   ├── application/     # Use cases + Port interfaces (single target, folder-grouped like adapters/)
│   │   ├── filesystem/  # FileNavigationUseCase, IFileSystemRepository, FileTransferUseCase, BulkRenameUseCase
│   │   ├── tags/        # TagManagementUseCase, ITagRepository
│   │   ├── media/       # MediaProcessingUseCase, IMediaDecoder, FilePreviewUseCase
│   │   ├── git/         # GitStatusUseCase, GitBranchUseCase, IGitStatusProvider, IGitBranchProvider
│   │   ├── monitoring/  # DirectoryWatchUseCase, FolderMonitorUseCase, IDirectoryWatcher, IFolderMonitor
│   │   ├── diskusage/   # DiskUsageUseCase, IDiskUsageScanner, IDiskUsageRepository
│   │   ├── duplicates/  # DuplicateFinderUseCase, IDuplicateFileScanner
│   │   └── fragmentation/ # FragmentationUseCase, IFragmentationScanner, IFragmentationRepository
│   ├── adapters/
│   │   ├── viewmodels/  # ViewModels/controllers (Qt-facing; §2.3, §14; single target, folder-grouped like application/)
│   │   │   ├── filesystem/ # FileListModel, FolderTreeModel, FileOperationsController, FileTransferService/Worker, BulkRenameViewModel, IconLoadingService/Worker, NavigationHistory, ViewMode
│   │   │   ├── workspace/  # WorkspaceController, WorkspacePaneViewModel, WorkspaceLayoutTopology, TabViewModel, SplitLayout, WorkspaceConfig (§14.60)
│   │   │   ├── tags/       # TagListViewModel, TagManagerViewModel, TagChangeNotifier, FileTagsService/Worker, TagColorPalette
│   │   │   ├── media/      # FilePreviewViewModel, FilePreviewWorker
│   │   │   ├── git/        # GitStatusService, GitStatusWorker (§14.32/§14.45)
│   │   │   ├── monitoring/ # MonitoringPanelViewModel, DirectoryWatchService
│   │   │   ├── diskusage/  # DiskUsageService/Worker, DriveSpaceInfoWorker, FolderSizeViewModel (§14.40)
│   │   │   ├── search/     # AdvancedSearchService/Worker, SearchResultsTreeModel
│   │   │   ├── duplicates/ # DuplicateFinderService/Worker, DuplicateGroupsModel (§14.51)
│   │   │   └── fragmentation/ # FragmentationService/Worker, FragmentationViewModel, FragmentationResultsModel
│   │   ├── persistence/ # SQLiteTagRepository + migrations
│   │   ├── filesystem/  # StandardFileSystemRepository
│   │   ├── shell/       # ShellContextMenuProvider
│   │   ├── config/      # AppConfigStore (JSON session persistence), WorkspaceStore (§14.60)
│   │   ├── favorites/   # FavoriteItem / FavoriteCategory / Favorites (§14.36)
│   │   ├── notifications/ # NotificationType / NotificationSettings / NotificationCenter (§14.37)
│   │   ├── media/       # QtMediaDecoder / FFmpegMediaDecoder (not yet implemented)
│   │   ├── archive/     # ArchivePathResolver / LibArchiveReader / ArchiveIndexCache (§14.29)
│   │   ├── diskusage/   # WindowsDiskUsageScanner / SQLiteDiskUsageRepository (§14.40)
│   │   ├── duplicates/  # StandardFileHasher / StandardDuplicateFileScanner (§14.51)
│   │   ├── git/         # LibGit2Repository / LibGit2StatusProvider (§14.32)
│   │   └── logging/     # Logging (spdlog wrapper: rotating file + console sinks, §14.23)
│   ├── ui/
│   │   ├── widgets/     # Qt Widgets shell (single target, folder-grouped like adapters/)
│   │   │   ├── core/    # MainWindow, WorkspaceLayoutWidget, SettingsDialog, UiTheme, FlowLayout
│   │   │   ├── navigation/ # WorkspacePaneWidget, FileBrowserView, FolderTreeView, delegates, context menus
│   │   │   ├── search/  # SearchCriteriaPanel, SearchResultsPane, SearchPanelWidget
│   │   │   ├── tags/    # TagChipWidget, TagPanelWidget, TagManagerDialog
│   │   │   ├── preview/ # PreviewPanelWidget, MediaPreviewWidget, TextPreviewWidget, SyntaxHighlighter
│   │   │   ├── video/   # VideoPlayerWidget (§14.67) -- standalone, reusable beyond the Preview tab
│   │   │   ├── diskusage/ # PieChartWidget, FolderSizeWidget
│   │   │   ├── fragmentation/ # FragmentationWidget
│   │   │   ├── duplicates/ # DuplicateFinderDialog
│   │   │   ├── monitoring/ # MonitoringLogModel, MonitoringPanelWidget
│   │   │   ├── terminal/ # TerminalWidget, TerminalSessionsWidget
│   │   │   ├── favorites/ # ManageFavoritesDialog
│   │   │   ├── decoration/ # FileDecorationRuleDialog
│   │   │   ├── config/  # EnvironmentVariablesDialog, EditEnvironmentVariableDialog
│   │   │   └── notifications/ # ToastNotificationWidget, ProgressNotificationWidget, NotificationOverlayWidget
│   │   │   # core/ also holds WorkspaceManagerDialog and WorkspaceLaunchContext (§14.60)
│   │   └── qml/         # unused -- no QML surface exists; inline video playback lives in
│   │                    # widgets/video/VideoPlayerWidget instead (§14.67)
│   └── app/
│       ├── CompositionRoot.cpp/.h
│       ├── ExpLorerApplication.cpp/.h   # QApplication subclass catching event-loop exceptions (§14.23)
│       ├── ExceptionHandler.cpp/.h      # fatal-exception dialog + logging + exit (§14.23)
│       ├── WorkspaceInstanceGuard.cpp/.h # per-workspace single-instance mutex + activation IPC (§14.60)
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
  * **App-wide singletons** (`MainWindow`, `WorkspaceLayoutWidget`, both `SidePanelWidget`s, `FolderTreeView`,
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

## 14. Feature Decision Log

Everything from this point on extends the UI/Adapters layers only — **no Domain or Application
layer redesign** — and is recorded as a decision log per feature, not an implementation
walkthrough; see git history and the code's own comments (many cite these subsection numbers
directly) for the detail.

The log grew too large for one document and is split across nine files under `docs/features/`,
organized **by feature topic** rather than by when each was built. Section numbers (§14.1–§14.78)
were assigned chronologically as each feature shipped and are kept stable within whichever file
now holds them, since code comments cite them directly — so a file's own section numbers are not
necessarily contiguous; each file's intro lists its exact set. **Keep each file at or under 1000
lines** — if a file would grow past that, split its most self-contained sub-topic into a new file
(picking the next free number in `docs/features/`, e.g. `10-...`) rather than letting it keep
growing, and update both that file's intro and this index to match.

* **[features/01-workspaces-and-panels.md](features/01-workspaces-and-panels.md)** — split-window
  layout/panes/tabs, session persistence, the bottom/right/left collapsible panel shells (Terminal/
  Tags/folder Tree), left/right panel state persistence, named workspaces (multi-instance launch).
* **[features/02-file-operations-and-navigation.md](features/02-file-operations-and-navigation.md)**
  — clipboard file operations, opening files, custom context menu, "This PC", multi-selection,
  address bar autocomplete & environment-variable expansion, hidden files, custom copy/move with
  progress notifications.
* **[features/03-search.md](features/03-search.md)** — per-tab filename/advanced/tag-integrated
  search, the unified left-panel Search tab, async advanced search, quick-search match-mode/
  skip-git-ignored toggles, debounced quick search, advanced-search match-mode toggles.
* **[features/04-tagging.md](features/04-tagging.md)** — the tag panel and selection tracking, the
  Tag Manager dialog, tag panel keyboard-driven autocomplete.
* **[features/05-preview-and-decoration.md](features/05-preview-and-decoration.md)** — the
  right-panel file Preview tab (and its later per-kind-widget/layout rework), text-preview syntax
  highlighting, archive browsing/preview/extraction, per-file/folder font & color decoration, the
  built-in Widgets video player.
* **[features/06-status-and-monitoring.md](features/06-status-and-monitoring.md)** — per-pane and
  window-wide status bars, per-file git status badges, live directory update, recursive folder
  monitoring, git branch status/checkout.
* **[features/07-media-viewer.md](features/07-media-viewer.md)** — Media Viewer mode (recursive
  tag/name-filtered image/video gallery) and its follow-ups: thumbnail previews/metadata overlays,
  decoder merge, video hover-scrub, loading progress, kind-scoped loading, sort/view/filmstrip/
  details controls, folder-grouped browsing, inline rename, thumbnail-decode performance (batched
  multi-frame decode, parallel decode pool).
* **[features/08-disk-and-file-tools.md](features/08-disk-and-file-tools.md)** — customizable
  Details-view columns (+ per-tab persistence), disk usage/folder size, duplicate file finder, disk
  fragmentation viewer (+ Details-view fragments column), bulk/batch rename.
* **[features/09-app-infrastructure.md](features/09-app-infrastructure.md)** — startup/exit
  logging & fatal exception handling, favorites, notification system, Light/Dark theme, per-process
  environment variables.

