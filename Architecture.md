Here is an architecture document for your C++ Advanced File Explorer, structured around the principles of Clean Architecture.

# Architecture Document

## 1. Architectural Approach

Project strictly adheres to **Clean Architecture** principles. The core philosophy is the **Dependency Rule**: source code dependencies must point inward toward the core business logic. External frameworks (Qt, SQLite, OS APIs) are treated as plugins to the core application.

This approach ensures the application is highly testable, independent of the UI framework, independent of the database, and resilient to changes in external libraries.


## 2. Layer Breakdown

The system is divided into four concentric layers, from the innermost core to the outermost mechanisms.

### 2.1 Domain Layer (Enterprise/Core Business Rules)

This layer contains the pure C++ plain data structures and core business logic. It has **no dependencies** on Qt, SQLite, or OS-specific APIs.

* `FileNode`: Entity representing a file or directory. Contains properties like `path`, `size`, `creationDate`, `modificationDate`, `fileType`, `hash` (for tracking moves/renames), and an optional `displayName` override (used only by synthetic entries such as drives under the "This PC" virtual location, see §14.15 — `std::nullopt` for every ordinary file/folder, which keeps `name()`/`path().filename()` authoritative everywhere else).
* `Tag`: Entity representing a user-defined tag. Contains `id`, `name`, and `hexColor`.
* `MediaMetadata`: Entity representing media properties (e.g., `resolution`, `duration`, `codec`).
* `FileTagAssociation`: Entity mapping a `FileNode`'s hash/path to a `Tag`'s ID.

### 2.2 Application Layer (Use Cases)

This layer orchestrates the flow of data to and from the entities. It defines the interfaces (Ports) for external systems to implement. It depends only on the Domain layer.

**Core Use Cases (Interactors):**

* `FileNavigationUseCase`: Handles fetching directory contents, sorting, filtering by extension, and basic CRUD file operations.
* `TagManagementUseCase`: Handles creating, updating, deleting, and assigning tags to files. Resolves broken paths by checking file hashes. Also aggregates tags for the tag panel: `tagsForPathWithAncestors` (a path's own tags plus every ancestor directory's tags, outermost ancestor first) and `searchTags` (ranks `allTags()` against a query: exact match, then prefix, then substring, each group alphabetical).
* `MediaProcessingUseCase`: Manages the extraction of thumbnails and basic metadata for images and videos.

**Gateway Interfaces (Ports):**

* `IFileSystemRepository`: Interface for OS file operations (list, move, delete, copy, hash generation, single-path `stat` — resolving one path to a `FileNode` without listing its parent directory, used to treat a browsed folder itself as a taggable target — and `openWithDefaultApplication`, launching the OS-registered handler for a file, see §14.11).
* `ITagRepository`: Interface for persisting tags and associations.
* `IMediaDecoder`: Interface for decoding media streams and extracting thumbnails.
* `IContextMenuProvider`: Interface for building a custom, registry-sourced file/folder context menu and invoking whichever entry the user picks (see §14.13) — not for displaying the OS's own popup. Two build entry points — one for a file/folder selection, one for empty-space-in-a-folder ("background") — mirroring the different registry roots consulted for each, plus `invoke`/`discardMenu` since building the entry list and showing/running it are separate steps once the popup itself is Qt-drawn.

### 2.3 Interface Adapters Layer

This layer converts data from the format most convenient for the use cases and entities to the format most convenient for the external frameworks (like Qt Views or SQLite).

**Controllers / ViewModels (Qt Models):**
* `FileListModel`: a single `QAbstractTableModel` (columns: Name, Size, Type, Date modified) wrapping directory-listing results (`FileNode` lists) supplied by `NavigationViewModel`. One model feeds both `QListView` (icon/list/tiles view modes, which only read column 0) and `QTreeView` (details view mode, all columns) — see §2.3.1. This supersedes an earlier sketch of two separate `FileTreeViewModel`/`FileGridViewModel` types: a single shared model matches how Explorer itself works and avoids duplicating file→row mapping logic.
* `ViewMode` (`src/adapters/viewmodels/ViewMode.h`): plain enum — `ExtraLargeIcons`, `LargeIcons`, `MediumIcons`, `SmallIcons`, `List`, `Details`, `Tiles`. Presentation-only state, currently held on `NavigationViewModel` (see §2.3.1, §14.1).
* `TagListViewModel`: `QObject`-derived, single shared instance owned/retargeted by `WorkspaceController` (see §14.9) to whichever pane/tab has focus. Exposes three sections for the tag panel: `folderTags()` (the focused tab's current folder plus every ancestor's tags, via `TagManagementUseCase::tagsForPathWithAncestors`), `searchResults()` (live-filtered via `TagManagementUseCase::searchTags` as the user types, excluding tags already on the current target), and `selectedItemTags()` (tags of the focused tab's single selected entry, or the current folder itself when nothing is selected — no target, and no tags shown, when more than one item is selected; see §14.16). Slots `addTagToSelection`/`removeTagFromSelection`/`createAndAddTagFromQuery` drive `TagManagementUseCase`.
* `MediaPreviewViewModel`: `QObject`-derived, `Q_PROPERTY`-exposed state for the image/video viewer, consumed by a QML scene embedded via `QQuickWidget` inside the main Widgets window.


**Repository Implementations:**
* `SQLiteTagRepository`: Implements `ITagRepository`. Maps SQL queries and `sqlite3` (or `QtSql`) results to `Tag` entities.
* `StandardFileSystemRepository`: Implements `IFileSystemRepository` using C++17/20 `<filesystem>` (`std::filesystem`). On Windows, paths are handled as UTF-16 (`std::filesystem::path` natively) and long paths (>260 chars) are supported via the `\\?\` extended-length prefix; paths are converted to UTF-8 only at the SQLite/adapter boundary (see §6).
* `QtMediaDecoder` / `FFmpegMediaDecoder`: Implements `IMediaDecoder` to generate thumbnails and provide playback streams.


### 2.3.1 View Modes

The directory view supports seven modes, matching a conventional Explorer-style file manager:
Extra Large Icons, Large Icons, Medium Icons, Small Icons, List, Details, Tiles.

* A `QStackedWidget` (`src/ui/widgets/FileBrowserView`) switches between a `QListView` (icon/list/
  tiles modes) and a `QTreeView` (details mode), both bound to the same `FileListModel`.
* Icon-mode sizing steps: Extra Large 256px, Large 96px, Medium 48px, Small 16px.
* List mode is `QListView::ListMode` with wrapping (multi-column text list, no grid); Details mode
  uses the `QTreeView`'s native column header (also wired to `FileNavigationUseCase::sortBy` on
  header-click).
* Tiles mode has no built-in Qt layout and uses a custom `QStyledItemDelegate`
  (`FileTileDelegate`) painting an icon plus two lines of text (name, then type/size).
* The four Icon-size modes (Extra Large/Large/Medium/Small) likewise use a custom
  `QStyledItemDelegate` (`FileIconDelegate`) rather than Qt's stock one: icon on top (sized from
  `QListView::iconSize()`, i.e. unchanged per the sizing steps above) with the item's name elided on
  one line underneath, and selection drawn as a dashed rectangle outline around the whole item
  instead of Qt's default filled highlight. List mode is the only `QListView`-hosted mode still using
  Qt's stock delegate.
* Icons come from `QFileIconProvider` (real OS shell icons), not bundled resources.
* View mode is UI/presentation state, not a Domain/Application concern — no new entities or Ports.
  It lives on `TabViewModel` (§14), one instance per tab, so each tab's view mode is independent of
  every other tab and pane.

### 2.4 Frameworks & Drivers Layer

The outermost layer composed of the actual UI, database engine, and external libraries.

* **UI Framework (Qt 6):** Hybrid UI — **Qt Widgets** for the main application shell (directory tree/grid, tag panel, menus, dialogs, drag-and-drop, window management), and **Qt Quick/QML** (embedded via `QQuickWidget`) for the image/video viewer, taking advantage of GPU-accelerated compositing, smooth zoom/pan/rotate transforms, and fullscreen playback overlays.
* **Database Engine:** SQLite runtime environment.
* **Media Engines:** Qt Multimedia (for standard playback) and/or FFmpeg (for robust codec support).
* **Operating System:** Windows/Linux kernel interacting with the file system.

---

## 3. Component Relationships & Data Flow

To illustrate the Clean Architecture flow, here are two primary operation sequences:

### Scenario A: User navigates to a directory and views files

1. **UI (Qt):** User clicks a folder in the `QTreeView`.
2. **Adapter (ViewModel):** UI invokes `navigateTo(path)` on the `NavigationViewModel`.
3. **Application (Use Case):** ViewModel calls `listDirectory(path)` on the `FileNavigationUseCase`.
4. **Application -> Adapter (Port/Repo):** The Use Case asks the `IFileSystemRepository` to fetch the files.
5. **Adapter -> Framework (OS):** `StandardFileSystemRepository` uses `std::filesystem::directory_iterator` to read the disk and returns a list of `FileNode` entities.
6. **Application -> Adapter:** The Use Case passes the `FileNode` list back to the `NavigationViewModel`, which emits `directoryContentsChanged`.
7. **Adapter -> UI:** `FileListModel` (fed by that signal) formats the entities into Qt roles (DisplayRole, DecorationRole) and emits `dataChanged` signals, updating whichever view (`QListView`/`QTreeView`) is currently shown.

### Scenario B: User tags a file

1. **UI (Qt):** User drags a "Work" tag onto "report.pdf".
2. **Adapter (Controller):** UI sends an `assignTag(fileId, tagId)` command to the Controller.
3. **Application (Use Case):** Controller triggers `TagManagementUseCase.assignTag(fileNode, tag)`.
4. **Application (Use Case):** The Use Case checks business rules (e.g., preventing duplicate tags on the same file). If the file lacks a unique hash, it calls `IFileSystemRepository.generateHash(fileNode)`.
5. **Application -> Adapter (Port/Repo):** The Use Case calls `saveAssociation(fileHash, tagId)` on the `ITagRepository`.
6. **Adapter -> Framework (Database):** `SQLiteTagRepository` executes an `INSERT INTO FileTags ...` SQL statement.

---

## 4. Main Components Overview

| Component Category | Component Name | Description |
| --- | --- | --- |
| **Core Entities** | `FileNode`, `Tag`, `MediaInfo` | Pure C++ structures holding state and basic validation. |
| **Use Cases** | `FileExplorer`, `TagManager` | Orchestrates file manipulation, tag searching, and data flow. |
| **Data Repositories** | `SQLiteTagDB`, `OSFileSystem` | Adapters handling raw data reading/writing (SQL and disk I/O). |
| **Media Adapters** | `ThumbnailGenerator`, `StreamProvider` | Bridges FFmpeg/QtMultimedia to abstract `IMediaDecoder` interface. |
| **Presentation** | `MainView`, `FileBrowserView`, `FileTileDelegate`, `TagPanelWidget`, `TagChipWidget`, `MediaPreviewPane` | Qt-based UI elements binding to ViewModels. `FileBrowserView` switches between `QListView`/`QTreeView` per `ViewMode` (see §2.3.1). `TagPanelWidget` renders `TagListViewModel`'s three sections using `TagChipWidget` (read-only/addable/removable chip, see §14.9). |
| **Workspace/Session** | `WorkspaceController`, `WorkspacePaneViewModel`, `TabViewModel`, `FileOperationsController` | UI-session state for the fixed 4-pane split-window layout (see §14); no business rules, no Domain/Application changes. `FileOperationsController` (see §14.10) drives Ctrl+C/X/V/Delete hotkeys via the real OS clipboard, and (see §14.13) builds/invokes entries for the app's own custom right-click context menu. |
| **Shell Integration** | `ShellContextMenuProvider`, `ContextMenuBuilder` | `ShellContextMenuProvider` implements `IContextMenuProvider` by reading Windows Registry verbs (and, optionally, COM shell-extension `IContextMenu` handlers) into plain `ContextMenuEntry` data — no OS popup involved; see §14.13. `ContextMenuBuilder` (`src/ui/widgets`) converts that data into an actual `QMenu`. |

---

## 5. Concurrency and Threading Model

To satisfy the non-functional requirement for responsiveness, the architecture enforces strict threading rules:

* **Main Thread (UI Thread):** Strictly reserved for the Frameworks Layer (Qt Event Loop) and ViewModels. No direct disk or database I/O is permitted here.
* **Worker Threads (Background Tasks):** * **I/O Thread Pool:** Managed by the Application Layer. Operations invoked on `IFileSystemRepository` (directory scanning, large file copying) and `ITagRepository` (database queries) are dispatched to background threads (using `std::async`, C++20 coroutines, or `QThreadPool`).
* **Media Thread:** Dedicated thread for `IMediaDecoder` to decode video frames or generate image thumbnails without stuttering the UI.


* **Data Synchronization:** Callbacks or Qt Signals/Slots (using queued connections) are used to safely pass `FileNode` entities or playback frames from worker threads back to the Main Thread ViewModels.
* **Database Concurrency:** The `SQLiteTagRepository` will configure SQLite in Serialized mode (Thread-safe) or utilize a single dedicated database thread to prevent `SQLITE_BUSY` or locking issues during concurrent reads (searching tags) and writes (bulk tagging). This addresses in-process (multi-threaded) concurrency only; cross-process concurrency is prevented separately by the single-instance enforcement described in §6.

---

## 6. Technology, Language & Build Decisions

* **C++ Standard:** C++20 (required for coroutine-based async I/O in the Application layer, `std::span`, and `std::filesystem` improvements). Compilers: MSVC v143+ (Windows), GCC 12+/Clang 15+ (Linux).
* **Initial Target OS:** Windows first (primary development and QA target). Linux support follows once the Windows build is stable; all platform-specific code must go through the `IFileSystemRepository`/OS-abstraction ports so a Linux backend can be added without touching the Domain/Application layers.
* **UI Toolkit:** Qt 6 (Widgets + Quick/QML hybrid, see §2.4).
* **Package/Dependency Manager:** **vcpkg** in manifest mode (`vcpkg.json` at repo root). All third-party dependencies (Qt6, SQLite3, FFmpeg, spdlog, GoogleTest/GoogleMock, xxHash) are declared there and consumed via `CMakeLists.txt` using `find_package`. A `CMakePresets.json` should pin the vcpkg toolchain file so a fresh clone only needs `cmake --preset <preset>` to configure.
* **Logging:** `spdlog` (with `spdlog::sinks::rotating_file_sink` for on-disk logs under the app-data log directory, plus a console sink in debug builds). A thin `ILogger`-free wrapper (`Logging::log(...)`) is used at call sites so spdlog is not directly referenced outside a single adapter header, keeping the Domain/Application layers framework-agnostic.
* **Dependency Injection:** No DI framework. A single **composition root** (`src/app/CompositionRoot.cpp`, invoked from `main.cpp`) constructs concrete repositories/adapters and injects them into use cases via constructor injection. Use cases and ViewModels only ever depend on interfaces (Ports) from the Application layer.
* **Path Handling & Long Paths:** Internally, paths are carried as `std::filesystem::path` (native UTF-16 on Windows, native encoding on Linux) through the Domain/Application/Adapters layers — never as raw `std::string` — to avoid lossy conversions. Long paths (>260 characters) are supported on Windows via the `\\?\` extended-length prefix applied in `StandardFileSystemRepository`; callers do not need to know about the prefix. Conversion to UTF-8 happens only at the SQLite storage boundary (`SQLiteTagRepository`), since SQLite stores/compares `TEXT` as UTF-8; the round-trip (UTF-8 → `std::filesystem::path`) happens when rows are read back.
* **Single-Instance Enforcement:** The application enforces a single running instance per user session using an OS-level primitive — a named mutex (`CreateMutexW`) on Windows, or a `flock`'d PID file under the app-data directory on Linux (see §9). If an instance is already running, the new process forwards its startup arguments (e.g. a path to open) to the existing instance via a local IPC mechanism (e.g. `QLocalSocket`/`QLocalServer`) and exits; the existing instance brings its main window to the foreground. This exists specifically to prevent two processes from writing to `explorer.db` and the thumbnail cache concurrently (see §5, §8).

---

## 7. Database Schema (SQLite)

The tagging database is a single SQLite file stored at the app-data location (see §9). Suggested initial schema (managed via versioned migration scripts run at startup, e.g. `PRAGMA user_version`):

```sql
CREATE TABLE Tags (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    hex_color   TEXT NOT NULL,           -- e.g. "#FF8800"
    created_at  INTEGER NOT NULL         -- unix epoch seconds
);

CREATE TABLE Files (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL UNIQUE,  -- last known absolute path
    file_hash     TEXT NOT NULL,         -- see §8 hashing strategy
    size          INTEGER NOT NULL,
    modified_at   INTEGER NOT NULL,
    last_seen_at  INTEGER NOT NULL       -- updated on each successful resolve, used to prune stale rows
);
CREATE INDEX idx_files_hash ON Files(file_hash);

CREATE TABLE FileTags (
    file_id  INTEGER NOT NULL REFERENCES Files(id) ON DELETE CASCADE,
    tag_id   INTEGER NOT NULL REFERENCES Tags(id) ON DELETE CASCADE,
    PRIMARY KEY (file_id, tag_id)
);
```

* **Path resolution:** `TagManagementUseCase` looks up by `path` first; if the path is missing/changed, it falls back to `file_hash` (+ `size`) to relocate the row and update `path`, satisfying the "robust handling of renames/moves" requirement.
* **Bulk tagging:** wrapped in a single `BEGIN IMMEDIATE ... COMMIT` transaction to satisfy the reliability NFR.
* **Multi-tag AND filtering:** implemented via `GROUP BY file_id HAVING COUNT(DISTINCT tag_id) = <N>` over the requested tag IDs.

---

## 8. File Identity, Hashing & Thumbnail Caching

* **File identity hash:** A **fast partial hash**, not a cryptographic hash, since files can be large media (video). Computed as a combination of `file size + first 64KB + last 64KB + last-modified timestamp`, mixed with a non-cryptographic hash function (e.g. xxHash64). This is sufficient to detect renames/moves and to disambiguate most files, while remaining O(1) relative to file size. This is a heuristic, not a uniqueness guarantee — a documented limitation, not a bug: two distinct files with identical size/edges/mtime are treated as the same file (acceptable trade-off for a tagging feature, not a security boundary).
* **Thumbnail cache:** Persisted on disk under the app-local cache directory (Windows: `%LOCALAPPDATA%/Exp-LORer/thumbnails/`), keyed by the same file-identity hash. Cache entries store a small JPEG/WebP-encoded thumbnail plus the source file's `size`/`modified_at` so stale thumbnails are regenerated automatically when a file changes. An in-memory LRU (`QPixmapCache` or a custom fixed-byte-budget cache) sits in front of the disk cache for the currently visible viewport.
* **Cache eviction:** Disk cache is capped by total size (configurable, default 512MB); least-recently-accessed entries are pruned on startup or when the cap is exceeded.

---

## 9. Application Data Locations & Configuration

* **Windows:** Database + logs + thumbnail cache live under `%LOCALAPPDATA%/Exp-LORer/` (`explorer.db`, `logs/`, `thumbnails/`). Standard `QStandardPaths::AppDataLocation` / `QStandardPaths::CacheLocation` are used so this is portable to Linux (`~/.local/share/Exp-LORer`, `~/.cache/Exp-LORer`) without code changes.
* **User settings:** window geometry and the split-window session (layout, each pane's tabs —
  path/view mode/sort order — and the focused pane) persist via a JSON config file (`config.json`,
  same directory as `explorer.db`), written by `AppConfigStore` and restored at startup — see
  §14.14. `QSettings` (INI format for cross-platform consistency rather than the Windows registry)
  remains in use only for settings not yet migrated to `config.json`: the context-menu
  shell-extensions toggle (§14.13.5) today; bookmarks/pinned folders and thumbnail cache size limit
  are unimplemented in either mechanism as of this writing.
* **Recycle Bin / Trash:** "Delete" maps to the native OS trash where available — `IFileOperation`/`SHFileOperation` with `FOF_ALLOWUNDO` on Windows; deferred to the freedesktop.org Trash spec (`libgio`/manual `.local/share/Trash`) when Linux support is implemented. A separate explicit "Delete Permanently" bypasses trash. Both go through `IFileSystemRepository` so the Application layer is unaware of the OS-specific mechanism.

---

## 10. Project Directory Structure

```
Exp-LORer/
├── CMakeLists.txt              # top-level, adds subdirectories below
├── CMakePresets.json            # pins vcpkg toolchain + generator per platform
├── vcpkg.json                   # manifest: qt6, sqlite3, ffmpeg, spdlog, gtest, xxhash
├── src/
│   ├── domain/                  # Entities: FileNode, Tag, MediaMetadata, FileTagAssociation (no Qt/SQLite deps)
│   ├── application/              # Use cases + Port interfaces (IFileSystemRepository, ITagRepository, IMediaDecoder)
│   ├── adapters/
│   │   ├── viewmodels/           # TabViewModel, NavigationHistory, FileListModel, ViewMode,
│   │   │                         # TagListViewModel, TagColorPalette, MediaPreviewViewModel,
│   │   │                         # SplitLayout, WorkspacePaneId, WorkspaceLayoutTopology,
│   │   │                         # WorkspacePaneViewModel, WorkspaceController,
│   │   │                         # FileOperationsController (see §14, §14.9, §14.10)
│   │   ├── persistence/          # SQLiteTagRepository, migration scripts
│   │   ├── filesystem/           # StandardFileSystemRepository (std::filesystem + OS trash calls)
│   │   ├── shell/                # ShellContextMenuProvider (registry/COM-sourced context menu data, see §14.13)
│   │   ├── config/                # AppConfig, AppConfigStore (JSON session/window-geometry persistence, see §14.14)
│   │   └── media/                # QtMediaDecoder / FFmpegMediaDecoder, ThumbnailGenerator
│   ├── ui/
│   │   ├── widgets/               # MainWindow, FileBrowserView, FileTileDelegate, TagPanelWidget,
│   │   │                          # TagChipWidget, dialogs (Qt Widgets, .ui files),
│   │   │                          # WorkspaceLayoutWidget, WorkspacePaneWidget, ContextMenuBuilder
│   │   │                          # (see §14, §14.9, §14.13)
│   │   └── qml/                   # MediaViewer.qml and supporting QML components
│   └── app/
│       ├── CompositionRoot.cpp/.h # wires concrete adapters into use cases
│       └── main.cpp
├── tests/
│   ├── domain/                   # pure unit tests, no mocks needed
│   ├── application/               # use case tests against mocked Ports (GoogleMock)
│   └── adapters/                  # SQLite repository tests (in-memory `:memory:` DB), filesystem tests (temp dirs)
└── resources/                     # icons, qml.qrc, default tag color palette
```

---

## 11. Testing Strategy

* **Framework:** GoogleTest + GoogleMock, integrated via CTest (`enable_testing()` / `gtest_discover_tests`).
* **Domain layer:** plain unit tests, no fixtures needed (pure data + validation logic).
* **Application layer:** use cases tested against **mocked Ports** (`MockFileSystemRepository`, `MockTagRepository`, `MockMediaDecoder`) generated with GoogleMock — this is the primary regression-safety net and must not require Qt, SQLite, or disk access.
* **Adapters layer:** `SQLiteTagRepository` tested against a real SQLite `:memory:` database (schema applied via the same migration scripts as production); `StandardFileSystemRepository` tested against `std::filesystem` temp directories, cleaned up per test.
* **UI layer:** out of scope for automated unit tests in v1; smoke-tested manually. (Optional future work: Qt Test for ViewModel-to-widget bindings.)
* **OS-shell side effects with no assertable return** (`ShellExecuteW` for opening a file, §14.11;
  `ShellContextMenuProvider`'s registry/COM-sourced menu building and invocation, §14.13): not
  covered by automated adapter tests — registry contents and installed shell extensions are live,
  per-machine state, and invoking an entry launches a real external process/COM object with no
  meaningful return value to assert in an automated run, so they're manually smoke-tested only
  (both the static-verbs-only and static-plus-shell-extensions modes). The `FileNavigationUseCase`
  passthrough in front of each is still covered by an ordinary mock-based use-case test.
* **Workspace/pane logic:** `WorkspaceLayoutTopology::visiblePanes()` (pure `SplitLayout`→pane-list
  mapping) is unit-tested under `tests/adapters/`, following the `NavigationHistoryTest.cpp` pattern.
  `WorkspaceController`/`WorkspaceLayoutWidget`/`WorkspacePaneWidget`'s `QSplitter`-building and
  focus-forwarding stay UI-only and untested per the UI-layer convention above.
* **CI expectation:** every use case and repository interface implementation must have corresponding tests before merging; `ctest` must pass in the build pipeline.

---

## 12. Coding Standards

* **Naming:** `PascalCase` for types/classes, `camelCase` for methods/variables, `m_` prefix for private member variables, `I` prefix for pure-interface (Port) classes (e.g. `IFileSystemRepository`).
* **Headers:** `#pragma once`; one class per header/source pair; Domain and Application headers must not `#include` any Qt, SQLite, or FFmpeg header.
* **Error handling:** Domain/Application layers signal failures via `std::expected<T, Error>` (or a project `Result<T>` alias if the toolchain lacks `std::expected`) rather than exceptions, so ViewModels can present errors without try/catch scattered through UI code. Adapters may catch third-party exceptions (e.g. SQLite exceptions) at the boundary and translate them into `Result` errors.
* **Formatting:** `.clang-format` (LLVM-based style, 4-space indent) checked into the repo root; enforced via a pre-commit hook or CI lint step.

---

## 13. Explicit v1 Scope Exclusions (Assumptions)

To keep the initial implementation bounded, the following are **out of scope for v1** unless requirements are revisited:

* Undo/redo for file operations (delete/move/rename) and tag assignment.
* Linux-specific trash integration (Windows trash ships first; Linux falls back to permanent delete until implemented).
* Automated UI testing.
* Internationalization/localization (UI strings are English-only for v1; `tr()` wrapping is still used so it isn't precluded later).

---

## 14. Split-Window Extension (Fixed Layouts, Per-Pane Tabs)

This section extends the architecture to support a fixed set of window-split layouts, each pane
owning its own tabs, alongside the existing navigation/tagging/media-viewer design. It does not
modify the Domain or Application layers.

### 14.1 Scope and Product Decisions

**Splits own tabs, not the reverse.** A fixed `SplitLayout` (`Single`, `TwoVertical`,
`TwoHorizontal`, `FourGrid` — exactly these four, no arbitrary/recursive nesting) is chosen first
and produces up to 4 fixed pane slots (`WorkspacePaneId`: `PaneA`..`PaneD`). Each pane slot
independently owns its own ordered list of tabs (add/close/activate) and its own navigation toolbar
(back/forward/up + address bar + view-mode dropdown) — fully independent per pane; there is no
shared/global navigation toolbar.

Panes persist their tab state even when hidden by a layout that shows fewer panes (e.g. switching
`FourGrid` → `Single` keeps `PaneB`/`PaneC`/`PaneD`'s tabs alive, just not rendered) — switching
layouts never destroys ViewModel state, only rebuilds the `QSplitter` chrome around it.

The tag filter panel and media preview (not yet implemented; still `.gitkeep` placeholders in
`src/adapters/persistence` and `src/adapters/media`) remain single shared instances for the whole
window, retargeted to whichever pane/tab currently has focus. This section defines the retargeting
hook point on `WorkspaceController` only (§14.3); the panel/preview ViewModels and widgets
themselves are out of scope for this increment.

Session persistence (restoring layout/tabs/paths across restarts) is out of scope for this
increment — see §14.8.

### 14.2 Key Architectural Decisions

* **Domain and Application layers are unchanged.** Panes and tabs are pure UI-session state with no
  business rules — no new entities, no new Ports, no new use cases.
* **`FileNavigationUseCase` must remain stateless.** One shared instance serves every pane/tab
  concurrently; only ViewModels are instantiated per-tab/per-pane.
* **Exactly one `MainWindow`.** Panes and tabs render inside its single central widget; no
  pane/tab component may ever construct a second top-level window.
* **Splits own tabs, not the reverse.** `WorkspaceController` owns exactly 4 `WorkspacePaneViewModel`
  instances (one per `WorkspacePaneId`), always instantiated regardless of the active `SplitLayout`;
  the layout only controls which of the 4 are currently visible/arranged. There is no composite
  pane tree — the pane set is flat and fixed at exactly 4 slots.
* **Tag filter panel and media preview are single shared instances**, retargeted to whichever pane
  currently has focus, via `WorkspaceController` — not duplicated per pane.

### 14.3 New Components by Layer

**`src/domain/`, `src/application/`** — no changes.

**`src/adapters/viewmodels/`** (new/renamed files)
* `SplitLayout.h` — plain C++ enum class: `Single`, `TwoVertical`, `TwoHorizontal`, `FourGrid`. No
  Qt.
* `WorkspacePaneId.h` — plain C++ enum class: `PaneA`, `PaneB`, `PaneC`, `PaneD`. No Qt. Naming is
  deliberately geometry-agnostic (not `TopLeft`/`TopRight`/...) since the same slot plays a
  different visual role depending on the active `SplitLayout`.
* `WorkspaceLayoutTopology.h/.cpp` — pure function `std::vector<WorkspacePaneId>
  visiblePanes(SplitLayout)`. No Qt; unit-tested (§11).
* `NavigationHistory.h/.cpp` — unchanged; per-tab back/forward stack (Command pattern).
* `TabViewModel.h/.cpp` (renamed from `NavigationViewModel.h/.cpp`) — `QObject`; owns one
  `NavigationHistory` + one `ViewMode` + one `FileListModel` (owned here as a `QObject` child,
  exposed via `fileListModel()`) + current path — the full self-contained state of a single browser
  tab. `navigateTo/goUp/goBack/goForward/setViewMode`; emits `currentPathChanged`,
  `directoryContentsChanged`, `backAvailableChanged`, `forwardAvailableChanged`,
  `upAvailableChanged`, `navigationFailed`, `viewModeChanged`.
* `FileListModel.h/.cpp` — unchanged type, now constructed and owned by `TabViewModel` instead of
  `MainWindow`.
* `ViewMode.h` — unchanged.
* `WorkspacePaneViewModel.h/.cpp` — `QObject`; represents one fixed pane slot. Owns an ordered list
  of `TabViewModel*` (Qt-parented) + active-tab index + its own `WorkspacePaneId`. API:
  `addTab()/closeTab(index)/setActiveTab(index)/activeTab()/tabAt(index)/tabCount()/id()`; emits
  `tabAdded(index)`, `tabClosed(index)`, `activeTabChanged(index)`. Constructed with
  `FileNavigationUseCase&` purely to construct the `TabViewModel`s it owns.
* `WorkspaceController.h/.cpp` — the Mediator. Owns exactly 4 `WorkspacePaneViewModel` children
  (always instantiated) + current `SplitLayout` + focused `WorkspacePaneId`. API:
  `pane(WorkspacePaneId)/layout()/setLayout(SplitLayout)/focusedPane()/
  setFocusedPane(WorkspacePaneId)/focusedTab()`; emits `layoutChanged(SplitLayout)`,
  `focusedPaneChanged(WorkspacePaneId)`. This is the hook point future
  `TagListViewModel`/`MediaPreviewViewModel` retargeting attaches to (connect to
  `focusedPaneChanged` and each pane's `activeTabChanged`) — not built in this increment.

No composite pane tree, no layout-snapshot type, no session-store type exists in this increment
(see §14.8).

**`src/adapters/persistence/`, `src/adapters/media/`** — untouched (`.gitkeep` placeholders remain;
these are the future retargeting hook targets for the tag panel/media preview, not built here).

**`src/ui/widgets/`**
* `MainWindow.h/.cpp` (modified) — no longer owns a navigation ViewModel/model/view/tab-widget
  directly. Owns a `WorkspaceController*` (handed in by `CompositionRoot`/`main.cpp`) and hosts a
  `WorkspaceLayoutWidget` as its single central widget. The View menu gains a "Layout" submenu (4
  checkable actions, one per `SplitLayout`) in place of the old global view-mode actions (view mode
  is now per-pane/per-tab); gains a small "Layout" toolbar mirroring the same actions.
* `WorkspaceLayoutWidget.h/.cpp` (new) — owns all 4 `WorkspacePaneWidget` instances up front
  (created once, never destroyed for the widget's lifetime) and arranges them into nested
  `QSplitter`s per the active `SplitLayout` (via `WorkspaceLayoutTopology::visiblePanes` plus a
  small switch for orientation/nesting). Rebuilding the `QSplitter` tree on layout change
  reparents/hides existing pane widgets rather than destroying them, so tab/navigation state in
  hidden panes survives. Forwards Qt focus-in events to `WorkspaceController::setFocusedPane` (via
  `QApplication::focusChanged`), keeping `WorkspaceController` itself free of any `QWidget`
  dependency.
* `WorkspacePaneWidget.h/.cpp` (new) — one pane's full UI: its own navigation toolbar
  (back/forward/up actions + address `QLineEdit` + view-mode `QToolButton`/menu, structurally the
  per-pane counterpart of the old global toolbar) + a closable/addable `QTabWidget` of
  `FileBrowserView` pages, one per `TabViewModel` in its bound `WorkspacePaneViewModel`. Rebinds the
  toolbar/address-bar/view-mode actions to whichever `TabViewModel` is the pane's current active tab
  whenever the tab strip's current index changes.
* `FileBrowserView.h/.cpp`, `FileTileDelegate.h/.cpp` — unchanged.

**`src/app/CompositionRoot.h/.cpp`** (modified)
* Keeps the single shared `StandardFileSystemRepository` + `FileNavigationUseCase`.
* Replaces `createNavigationViewModel()` with three factory methods: `createTabViewModel()`,
  `createWorkspacePaneViewModel(WorkspacePaneId)`, `createWorkspaceController()` — each wires a
  fresh ViewModel to the shared use case. `WorkspaceController`'s own constructor independently
  builds its 4 owned `WorkspacePaneViewModel` children the same way, since `src/adapters` code must
  not call back into `src/app` — these granular factories exist for direct/future use, not because
  `WorkspaceController` routes through them.

### 14.4 Component Relationship

```
MainWindow (single top-level window)
 └── WorkspaceController (Mediator; owns current SplitLayout + focused pane)
      ├── WorkspacePaneViewModel (PaneA) ── TabViewModel #1 ── FileListModel #1
      │                                 └── TabViewModel #2 ── FileListModel #2
      ├── WorkspacePaneViewModel (PaneB) ── TabViewModel #1 ── FileListModel #1
      ├── WorkspacePaneViewModel (PaneC)  (0 tabs until the user reveals/populates it)
      ├── WorkspacePaneViewModel (PaneD)  (0 tabs until the user reveals/populates it)
      ├── [future] TagListViewModel (shared, 1 instance) ◄── retargeted to focused pane's active tab
      └── [future] MediaPreviewViewModel (shared, 1 instance) ◄── retargeted likewise
                                                                              │
                                                                              ▼
                                                   FileNavigationUseCase (shared, stateless)
                                                                              │
                                                                              ▼
                                                   IFileSystemRepository (shared, CompositionRoot)

WorkspaceLayoutWidget (UI) renders WorkspaceController's 4 WorkspacePaneViewModels as up to 4
WorkspacePaneWidgets, nested in QSplitters per the active SplitLayout; each WorkspacePaneWidget
renders its WorkspacePaneViewModel's tabs as FileBrowserView pages in its own QTabWidget.
```

### 14.5 SOLID / Pattern Rationale

* **SRP**: `TabViewModel` (a single tab's navigation+listing state) vs. `WorkspacePaneViewModel`
  (tab bookkeeping for one fixed slot) vs. `WorkspaceController` (cross-pane mediation + layout) are
  three distinct reasons to change.
* **OCP**: new pane-count layouts are added by widening `SplitLayout` + `WorkspaceLayoutTopology` +
  `WorkspaceLayoutWidget`'s switch, not by touching `WorkspacePaneViewModel`/`WorkspaceController`.
* **ISP**: Ports (`IFileSystemRepository` etc.) get no pane-aware methods — panes are
  presentation-only.
* **DIP**: `WorkspacePaneViewModel`/`WorkspaceController` never reference a sibling directly; they
  only emit signals that `WorkspaceLayoutWidget`/`WorkspacePaneWidget` subscribe to.

**Patterns used**: Factory Method (`CompositionRoot::createXViewModel()`), Mediator
(`WorkspaceController`), Observer (Qt signals/slots), Command (`NavigationHistory`'s back/forward
stacks, unchanged). Composite and Memento are dropped in this revision — there is no
`SplitPaneNode`/`WorkspaceLayoutSnapshot` in the fixed 4-layout model.

### 14.6 Usage Rules

**Must:**
* `FileNavigationUseCase` stays stateless so one shared instance safely serves concurrent calls from
  every tab/pane.
* Cross-pane/tab coordination goes through `WorkspaceController`; `TabViewModel`/
  `WorkspacePaneViewModel` communicate only via their own signals.
* `TabViewModel`/`WorkspacePaneViewModel`/`WorkspaceController` are created only via
  `CompositionRoot`'s factory methods, or — for tabs added during a running session — via
  `WorkspacePaneViewModel::addTab()`; UI widgets never call `new TabViewModel(...)`/
  `new WorkspacePaneViewModel(...)` themselves.
* `SplitLayout`/`WorkspacePaneId`/`WorkspaceLayoutTopology` stay plain C++ (no `QWidget`/`QObject`
  embedded); the `QSplitter` tree is built *from* them, separately, in `WorkspaceLayoutWidget`.
* Exactly one `MainWindow` per process; panes/tabs render only inside it.
* Focus changes retarget the shared Tag/MediaPreview ViewModels (once built) through
  `WorkspaceController::focusedPaneChanged` — no pane holds its own copy of either.
* All 4 `WorkspacePaneViewModel` instances exist for the lifetime of `WorkspaceController` regardless
  of `SplitLayout`; hidden panes keep their tabs.

**Must not:**
* No `WorkspacePaneId`/`SplitLayout` type may appear in any `src/domain` or `src/application`
  header.
* No pane/tab/workspace component may create its own `QThread`/`QThreadPool`/`std::thread`.
* `NavigationHistory` must not perform disk I/O itself.
* No arbitrary/recursive pane nesting — `SplitLayout` is a closed, 4-value enum; do not reintroduce
  a composite/tree pane model.

### 14.7 Navigation History and Threading

* **History is per-tab**: each `TabViewModel` owns exactly one `NavigationHistory`; closing a tab
  discards it; there is no global or pane-shared back/forward.
* **Threading**: unchanged from the base design — simultaneous navigations from different tabs/panes
  become independent tasks on the same shared `IFileSystemRepository`-owned pool; results marshal
  back via queued signals to the originating `TabViewModel`'s `FileListModel` only — no cross-tab
  bleed, no per-tab thread pools.

### 14.8 Deferred / Future Work

* **Session persistence** (restoring `SplitLayout` + each pane's tabs/paths/view modes/sort orders
  + window geometry across restarts) is now implemented — see §14.14. Live/continuous autosave
  during a session (as opposed to save-on-close) remains future work.
* **Tag filter panel / media preview retargeting**: hook point is
  `WorkspaceController::focusedPaneChanged` + `WorkspacePaneViewModel::activeTabChanged`; the
  ViewModels/widgets themselves remain `.gitkeep` placeholders.
* **Newly-revealed empty panes** (e.g. `PaneB` the first time a user switches from `Single` to
  `TwoVertical`) start with 0 tabs; the recommended default is to auto-seed one tab at the focused
  pane's current path so the user never sees a blank pane. An empty pane with just a "+" button is
  an acceptable, simpler fallback for v1 if auto-seeding is deferred.
* **Cross-pane drag-and-drop**: out of scope; `WorkspaceController` remains the natural future
  broker.

### 14.9 Tag Panel and Selection Tracking

Fulfills the §14.3/§14.8 retargeting hook: the shared `TagListViewModel` (§2.3) is now built and
owned by `WorkspaceController`, and `TabViewModel` gains a second piece of per-tab UI-session
state alongside navigation history — the currently selected row in that tab's `FileBrowserView`.

* **Selection is per-tab state, and multi-item since §14.16.** `TabViewModel::selectedEntries()`/
  `setSelectedEntries()`/`selectedEntriesChanged` (a `std::vector<FileNode>`, was a single
  `std::optional<FileNode>` before §14.16) mirror `currentPath`/`currentPathChanged`: owned by the
  tab, cleared on every `navigateTo`/`goBack`/`goForward`/`goUp` so a stale selection from the
  previous folder never leaks into the new one. `FileBrowserView` shares one `QItemSelectionModel`
  between its `QListView` and `QTreeView` (so switching `ViewMode` doesn't drop the selection), both
  set to `QAbstractItemView::ExtendedSelection` + `SelectRows` (§14.16), and emits
  `selectionChanged(std::vector<FileNode>)` built from `QItemSelectionModel::selectedRows()`, which
  `WorkspacePaneWidget` connects 1:1 to each page's own `TabViewModel::setSelectedEntries` at
  tab-creation time (unlike the toolbar, which rebinds to whichever tab is active, selection wiring
  doesn't need to rebind).
* **`TagListViewModel::setActiveTab(TabViewModel*)`** is `WorkspaceController`'s retargeting slot,
  invoked whenever `focusedPaneChanged` or any pane's `activeTabChanged` fires (and once at
  construction): it disconnects the previous tab's `currentPathChanged`/`selectedEntriesChanged`,
  connects the new one, and refreshes all three panel sections immediately.
* **Resolving the tagging target** (used for the panel's "+"/"x" actions and the
  selected-item-tags section): the single entry in `selectedEntries()` if exactly one item is
  selected; if zero are selected, the current folder itself — fetched via a new
  `IFileSystemRepository::stat`/`FileNavigationUseCase::stat` single-path lookup (distinct from
  `listDirectory`, which enumerates children, not the directory itself), letting a user tag the
  folder they're browsing without selecting a child row first; if more than one item is selected,
  there is no target — `addTagToSelection`/`removeTagFromSelection` report `operationFailed`
  instead. Multi-item tagging is not built (§14.16).
* **Persistence**: `SQLiteTagRepository` (§7, §10) is the first concrete `ITagRepository`
  implementation — the tag panel is the feature that required building it. `CompositionRoot` owns
  it plus a `TagManagementUseCase`, both threaded through `WorkspaceController`'s constructor.
* **Threading**: kept synchronous, consistent with `TabViewModel`'s existing (pre-existing, not
  introduced here) synchronous calls into `FileNavigationUseCase` — §5's background-dispatch
  model isn't implemented anywhere yet, and this feature doesn't newly diverge from that.

### 14.10 Keyboard Hotkeys and Clipboard File Operations

Adds keyboard-driven copy/cut/paste/delete/navigation to the file browser, plus the shared
component that backs copy/cut/paste. No Domain/Application changes — `FileNavigationUseCase`
already exposed every file operation this needs (`moveFile`/`copyFile`/`moveFileToTrash`/
`deleteFilePermanently`).

* **Capture mechanism**: `FileBrowserView` installs itself as a `QEvent::KeyPress` event filter on
  its two child views (`m_listView`, `m_treeView`) rather than using a `QShortcut`/`QAction`. A
  window- or widget-scoped shortcut for Ctrl+C would also fire while the address bar or tag search
  box has focus, breaking normal text editing there; filtering key events on just the two file-list
  views means the hotkeys are active only while the file list itself has keyboard focus, matching
  Explorer. Unhandled keys (anything but the hotkeys below) fall through to Qt's default
  processing.
* **Hotkeys** (`FileBrowserView` new signals `copyRequested`/`cutRequested`/`pasteRequested`/
  `deleteRequested(bool permanent)`/`navigateUpRequested`, consumed by `WorkspacePaneWidget`):
  Ctrl+C copy, Ctrl+X cut, Ctrl+V paste, Delete → move to Recycle Bin, Shift+Delete → permanent
  delete, Backspace → parent folder. In Details view only, ArrowLeft also goes to the parent folder
  and ArrowRight opens a selected folder (no-op on a file); icon/list/tiles modes leave arrow keys
  at their default grid-navigation behavior. Qt's standard `Cut` key sequence includes "Shift+Del"
  as an alternate binding on Windows, which file managers repurpose for permanent delete instead —
  `Qt::Key_Delete` is therefore checked before `QKeySequence::Cut` so Shift+Delete is never
  misread as Cut.
* **Letter-jump type-ahead** (typing `[a-z]` selects the next item starting with that letter,
  cycling on repeated presses) needs no new code: it's `QAbstractItemView::keyboardSearch()`,
  already built into both `QListView` and `QTreeView`, left unhandled by the event filter above.
* **`FileOperationsController`** (new, `src/adapters/viewmodels/`): a single shared instance owned
  by `WorkspaceController` (constructed alongside `TagListViewModel`; no per-tab retargeting needed
  since the clipboard is OS-global, not pane-scoped). `copyToClipboard`/`cutToClipboard` write the
  real Windows clipboard via `QMimeData::setUrls()` plus a `"Preferred DropEffect"` custom format
  (little-endian `quint32`, `DROPEFFECT_COPY`/`DROPEFFECT_MOVE`) so cut/copy/paste interoperate
  with File Explorer and other apps. `pasteInto(destinationDirectory)` reads the clipboard's file
  URLs, copies or moves each via `FileNavigationUseCase` (auto-renaming on a name collision, since
  `move`/`copy` both fail with `AlreadyExists` on one), and clears the clipboard after a successful
  cut-paste (a copied clipboard can be pasted repeatedly). `moveToTrash`/`deletePermanently` thinly
  wrap the matching `FileNavigationUseCase` calls; `WorkspacePaneWidget` shows a confirmation
  dialog before invoking either.
* **Cross-pane refresh**: `FileOperationsController` emits `directoryContentsMayHaveChanged(path)`
  after any successful operation. `WorkspaceController` refreshes every live tab (any pane, visible
  or hidden) whose `currentPath()` matches, via a new `TabViewModel::refresh()` slot that re-runs
  the existing `loadAndApply` funnel (clearing stale selection and re-validating against disk).
  This covers a cut/paste spanning two different panes.
* **Selection is multi-item since §14.16**: Ctrl+C/X/Delete act on the full
  `TabViewModel::selectedEntries()`, not a single item. `FileOperationsController::copyToClipboard`/
  `cutToClipboard` take a `std::vector<std::filesystem::path>` and write every path to the clipboard
  in one `QMimeData::setUrls()` call; `moveToTrash`/`deletePermanently` stay single-path, looped by
  the caller over the selection (same posture `pasteInto` already uses internally for a multi-URL
  paste).

### 14.11 Opening Files with the Default Application

Double-click/Enter on a file launches its OS-registered default application. No new key-capture
code: `FileBrowserView::itemActivated(path, isDirectory)` already fires on double-click and
Enter/Return (Qt's built-in `QAbstractItemView::activated`), and in Details view on ArrowRight for a
directory row only (§14.10) — `WorkspacePaneWidget` previously handled only the `isDirectory == true`
branch (navigate the tab); it now also handles `isDirectory == false` by opening the file.

* **Port**: `IFileSystemRepository::openWithDefaultApplication(path)` (see §2.2), implemented in
  `StandardFileSystemRepository` via `ShellExecuteW` on Windows — the same
  `Shellapi.h`/conditionally-linked `Shell32` already used for `moveToTrash`'s `SHFileOperationW`.
  Routed through the Port (rather than a widget calling `QDesktopServices::openUrl` directly) to
  keep the existing pattern: UI/adapters reach the OS shell only through
  `IFileSystemRepository`/`FileNavigationUseCase`, matching `moveFile`/`copyFile`/`moveFileToTrash`/
  `deleteFilePermanently`. `ShellExecuteW` hands off to the shell and returns immediately (unlike
  `SHFileOperationW`'s synchronous I/O), so this runs synchronously on the UI thread, consistent with
  the existing precedent for the other file operations.
* **`FileNavigationUseCase::openFile(path)`** thinly delegates to the Port, same shape as
  `moveFile`/`moveFileToTrash`.
* **`FileOperationsController::openFile(path)`** (new slot, alongside the §14.10 clipboard/delete
  slots) calls the use case and emits `operationFailed` on failure (e.g. no registered handler),
  reusing the existing status-bar wiring; it does not emit `directoryContentsMayHaveChanged` since
  opening a file doesn't change any directory's listing.
* **No automated adapter test** for the `ShellExecuteW` call itself — it launches a real external
  process with no meaningful assertable return in an automated run, so it's manually smoke-tested
  only (Architecture.md §11's existing carve-out pattern for OS-shell side effects).
  `FileNavigationUseCase::openFile`'s delegation is covered by a mock-based use-case test.

### 14.12 Sort Order (Per-Tab, All View Modes)

Lets the user choose how a tab's directory contents are ordered (Name/Size/Type/Date modified,
ascending/descending), applied uniformly across all seven view modes and preserved across
navigation within that tab. No Domain/Application change: `SortCriterion` and
`FileNavigationUseCase::sortBy` (§2.2) already existed and are reused as-is.

* **State lives on `FileListModel`**, not a new type: it is already owned 1:1 by `TabViewModel` and
  alive for the tab's whole lifetime, and it is already the single model shared by every view mode
  (§2.3.1), so sorting it once covers every view mode for free. `FileListModel::setEntries`
  re-applies the model's current `SortCriterion`/ascending flag via `FileNavigationUseCase::sortBy`
  to every freshly-listed directory, which is what makes a tab's sort choice survive navigation —
  no separate save/restore step exists or is needed. `TabViewModel` exposes thin passthrough
  accessors/mutators (`sortCriterion()`/`sortAscending()`/`setSortCriterion()`/
  `sortOrderChanged`), the same shape as its existing `viewMode()`/`setViewMode()`, so UI code
  never reaches into a pane's `FileListModel` directly.
* **Two convergent entry points, one code path**: `QTreeView`'s native header-click sorting
  (Details view, unchanged mechanism from §2.3.1) and the new View-menu "Sort by" submenu both
  funnel into `FileListModel::setSortCriterion(criterion, ascending)`. `FileBrowserView` keeps the
  `QTreeView` header's sort-indicator arrow in sync with menu-driven changes by connecting to
  `FileListModel::sortOrderChanged`; a no-op guard in `setSortCriterion` for an unchanged
  criterion/order prevents a feedback loop between the header's `sortIndicatorChanged` and this
  connection.
* **`WorkspaceController::focusedTabChanged(TabViewModel*)`** (new signal, emitted from the same
  internal slot that already retargets `TagListViewModel` per §14.9) is the hook `MainWindow` uses
  to retarget the View > Sort by submenu at the focused pane's active tab — connect/disconnect
  `sortOrderChanged` per retarget, same pattern §14.9 already established for
  `TagListViewModel::setActiveTab`. If the focused pane currently has 0 tabs (§14.8's empty-pane
  case), the submenu is disabled.
* **View menu, not a per-pane toolbar control**: unlike `ViewMode` (exposed only via each pane's
  own toolbar dropdown), sorting is exposed as a single global "Sort by" submenu under View,
  retargeted to whichever pane/tab has focus — matching the product ask that this be reachable from
  the View menu rather than duplicated per pane.
* **New tabs start at the default** (`Name`, ascending), not inherited from a sibling tab in the
  same pane — the same precedent as `TabViewModel::m_viewMode`'s hardcoded `ViewMode::Details`
  default.
* **Not built here**: persisting the chosen sort across app restarts — folds into the existing
  §14.8 session-persistence deferral (layout/tabs/paths/view-modes), not a separate future item.

### 14.13 Custom Context Menu (Registry-Sourced, Files/Folders/Background Differ)

Right-click on a file/folder row, or on empty space within a directory view, shows a **custom,
Qt-drawn `QMenu`** — not the real Windows shell popup. Its *content* still comes from the Windows
Registry (and, optionally, the same COM shell-extension mechanism Explorer uses), so per-file-type
entries and third-party additions (7-Zip, Git, "Open with VS Code", etc.) still show up, but the
menu itself is painted and controlled entirely by the app, not by `user32`/`TrackPopupMenuEx`. A
file selection, a folder selection, and empty folder background each consult different registry
roots and so can show genuinely different items (§14.13.1). Registry-sourced entries are merged
with a fixed set of app-native actions (Open/Cut/Copy/Paste/Delete/Rename/New Folder/Properties)
that this app already implements itself rather than delegating to the shell.

Two resolution modes exist, user-selectable via a View-menu toggle persisted in `QSettings`:
**static verbs only** (fast, no COM) and **static verbs + shell extensions** (also instantiates
registered `IContextMenu` COM handlers for closer Explorer parity, at the cost of COM activation
overhead per handler). Both are described below; neither shows/uses `TrackPopupMenuEx`.

#### 14.13.1 Registry Roots Consulted Per Target

* **File selection**: `HKCR\<ext>` → its ProgID → `HKCR\<ProgID>\shell`, plus `HKCR\<ext>\shell`
  (extension-level overrides), `HKCR\SystemFileAssociations\<ext or PerceivedType>\shell`, and
  `HKCR\*\shell` / `HKCR\AllFilesystemObjects\shell` (apply to every file). `HKEY_CLASSES_ROOT`
  is already the OS's own HKLM+HKCU-merged view, so no separate `HKCU\Software\Classes` pass is
  needed.
* **Folder row** (a folder selected inside a listing, not entered): `HKCR\Directory\shell` and
  `HKCR\Folder\shell`, plus `HKCR\AllFilesystemObjects\shell`.
  This is the concrete source of "files and folders have different items" — a folder row never
  sees `.ext`/ProgID verbs, and a file never sees `Directory\shell` verbs.
* **Folder background** (empty space in the view): `HKCR\Directory\Background\shell` only — a
  smaller, distinct verb set with no per-file verbs, but adding New/Paste/folder Properties.

#### 14.13.2 Port and Entities

* **`IContextMenuProvider`** (`src/application/IContextMenuProvider.h`) is reshaped around
  "build data, then invoke," since a Qt-drawn popup means *this app* controls the show/exec step,
  not a blocking native call:
  * `buildItemMenu(paths, mode)` / `buildBackgroundMenu(folder, mode)` → `Result<vector<ContextMenuEntry>>`.
    `paths` must all share one parent directory (Explorer never right-clicks a cross-folder
    selection — guaranteed here since `FileBrowserView` only ever lists one flat directory at a
    time); a genuine multi-item selection is passed since §14.16 (originally the vector stayed a
    vector even though only a single-item selection was passed, anticipating this).
  * `invoke(entryId, ownerWindow)` runs the entry returned by the most recent build call.
  * `discardMenu()` releases any COM handlers a build call kept alive, if the user closes the menu
    without picking a registry-sourced entry. Always safe to call.
  * `ContextMenuSourceMode` (`StaticVerbsOnly` / `StaticAndShellExtensions`) is a parameter on each
    build call, not Port-owned state — where the user's preference is stored is a UI-layer concern
    (§14.13.5).
  * `ContextMenuEntry` (id, label, `isSeparator`, `enabled`, `ContextMenuIcon`, nested `submenu`)
    and `ContextMenuIcon` (width/height/RGBA byte buffer) are plain data types defined alongside
    the Port, the same boundary precedent as `std::filesystem::path` crossing into Application —
    no `QIcon`/`QMenu` type ever appears above `src/ui/widgets`.
  * `NativeWindowHandle = void*` moves to a small shared `src/application/NativeTypes.h` (it's now
    also needed by `IFileSystemRepository::showProperties`, §14.13.4); `NativeScreenPoint` is
    dropped entirely — positioning the `QMenu` is a `QPoint`/UI-layer concern now, not something
    the Application layer needs to know about.
* **`FileNavigationUseCase`** passthroughs: `buildItemContextMenu`, `buildBackgroundContextMenu`,
  `invokeContextMenuEntry`, `discardContextMenu` (same shape as `openFile`'s passthrough to
  `IFileSystemRepository`), plus `createFolder`/`createFileFromTemplate`/`showProperties`
  (§14.13.4).

#### 14.13.3 `ShellContextMenuProvider` (Registry + Optional COM, No Native Popup)

Implements the Port. Keeps its existing name/platform-general-with-`#ifdef _WIN32`-internals
pattern (`StandardFileSystemRepository` precedent) even though it no longer shows a shell popup —
it still *sources* its data from the shell/registry. Non-Windows returns a `Result` failure ("Not
supported on this platform"), matching `openWithDefaultApplication`'s existing posture.

* **Static verb resolution** (always runs, both modes): for each applicable root (§14.13.1),
  enumerate `shell\<verb>` subkeys; read the label (`MUIVerb`, resolved via
  `SHLoadIndirectString` for `@dll,-id` references, else the key's default value), the `Icon`
  value (or the command's own exe), and the `command` subkey's default value as a command-line
  template (`%1`/`%L`/`%*` expanded against the target path(s) at invoke time). Verbs marked
  `LegacyDisable`, `ProgrammaticAccessOnly`, or `Extended` are skipped.
  * **Suppressed verb names**: `open` (default verb only — non-default "Open with `<App>`" entries
    still show), `cut`, `copy`, `paste`, `delete`, `rename`, `properties`. These already have
    app-native equivalents (§14.13.4) that the UI layer places at fixed positions instead, so the
    same action never appears twice wired to two different code paths.
  * **"Open with" submenu**: `HKCR\<ext>\OpenWithProgIds` + `HKCR\<ext>\OpenWithList` subkeys +
    `HKCU\...\Explorer\FileExts\<ext>\OpenWithList` (MRU), each resolved to a friendly name
    (`HKCR\Applications\<exe>\FriendlyAppName`, falling back to the exe's version info or bare
    name) + icon + `"<exe>" "%1"` command.
  * **"New" submenu** (background only): every `HKCR` `.*` subkey with a `ShellNew` child,
    supporting the common `NullFile` (empty file) and `FileName` (copy template) cases only —
    `Command`/binary `Data` ShellNew mechanisms are out of scope. Built once lazily and cached for
    the process's lifetime rather than rescanned per right-click. "New Folder" itself is a fixed,
    non-registry entry always first in this submenu.
* **Dynamic shell-extension resolution** (`StaticAndShellExtensions` mode only, appended after a
  separator): enumerate `shellex\ContextMenuHandlers` subkeys under the same applicable roots;
  for each CLSID, `CoCreateInstance` + `IShellExtInit::Initialize` (reusing the
  `SHParseDisplayName`/`BindToObject`/`GetUIObjectOf`-style resolution the old implementation used
  for this part) + `QueryInterface` for `IContextMenu`, then `QueryContextMenu` into a scratch
  `CreatePopupMenu()` handle using a per-handler id subrange (handler *N* → ids
  `[N*1000, N*1000+999)`, mirroring how Explorer offsets each handler's `idCmdFirst` so ids never
  collide across handlers). The populated scratch `HMENU` is walked via
  `GetMenuItemCount`/`GetMenuItemInfo` to build the equivalent `ContextMenuEntry` tree (including
  any icon the handler supplied). Each contributing handler's `IContextMenu*` is kept alive in a
  member map (id-subrange → interface) until the next build call or `discardMenu()`, since
  `invoke()` must call `InvokeCommand` on the *specific* handler that produced the chosen id.
* **Icon extraction** (both paths): resolve an icon reference (`"shell32.dll,-16769"` or a bare
  path) via `ExtractIconExW`/`PrivateExtractIconsW`, convert the `HICON` to a 32bpp RGBA buffer via
  `GetIconInfo` + `GetDIBits`. An unresolvable icon just leaves `ContextMenuIcon` empty rather than
  failing the entry.
* **`invoke(id, ownerWindow)`**: a static entry expands its stored command-line template against
  the target path(s) and launches it (`CreateProcessW`, falling back to `ShellExecuteW` for bare
  verbs/documents); a dynamic entry looks up its owning handler by id-subrange and calls
  `InvokeCommand` (via `CMINVOKECOMMANDINFOEX`, same Unicode handling as before).
* No explicit `CoInitializeEx` (same existing assumption: Qt's Windows platform plugin already
  initializes COM STA on the UI thread). Must only ever be called from the Qt UI thread.
* No more `TrackPopupMenuEx`/owner-draw message forwarding: since the popup is a `QMenu` now, the
  old `ContextMenuMessageFilter`/`QAbstractNativeEventFilter` machinery this section previously
  described is gone along with the `Qt6::Core` link it required in this adapter.

#### 14.13.4 New Native Actions (Rename, New Folder, New-from-template, Properties)

The old design got Cut/Copy/Paste/Delete/Rename/New/Properties "for free" from the shell popup;
replacing that popup means the app must provide the ones it didn't already (`FileOperationsController`
already had Cut/Copy/Paste/Delete/Open, §14.10–§14.11):

* **Rename**: no new Port method — it's `FileNavigationUseCase::moveFile(path, sameParent/newName)`
  behind a modal `QInputDialog::getText` (current name pre-filled, extension pre-selected). Inline
  in-grid rename-on-create is deferred (§14.13.6).
* **`IFileSystemRepository`** gains `createDirectory(directory)`, `createFileFromTemplate(dest, optionalTemplate)`
  (backs the "New" submenu's `NullFile`/`FileName` cases), and `showProperties(path, ownerWindow)`
  (`SHObjectProperties(hwnd, SHOP_FILEPATH, path.c_str(), nullptr)` on Windows, "Not supported on
  this platform" elsewhere — same posture as `openWithDefaultApplication`).
* **`FileNavigationUseCase`** gets matching thin passthroughs: `createFolder`,
  `createFileFromTemplate`, `showProperties`.

#### 14.13.5 UI Wiring

* **`FileOperationsController`** (`src/adapters/viewmodels`) stays a pure orchestrator — it does
  not construct any `QMenu`/`QAction` itself, keeping this layer's existing "ViewModels don't build
  widgets" convention. It replaces the old `showContextMenuForSelection`/`showContextMenuForFolder`
  slots with `buildContextMenuForSelection`/`buildContextMenuForFolder` (→
  `Result<vector<ContextMenuEntry>>`), `invokeContextMenuEntry`, `discardContextMenu`, plus
  `createFolder`/`createFileFromTemplate`/`showProperties` wraps (same `operationFailed`/
  `directoryContentsMayHaveChanged` signal pattern as the existing slots).
* **`ContextMenuBuilder`** (new, `src/ui/widgets/ContextMenuBuilder.h/.cpp`) is the one place that
  turns `vector<ContextMenuEntry>` plus a set of already-wired fixed native `QAction`s into an
  actual `QMenu*`: nested `QMenu`s for `submenu` entries, `QIcon` from each `ContextMenuIcon`'s RGBA
  buffer, separators from `isSeparator`, disabled state from `enabled`; interleaves the native
  actions at Explorer-conventional positions around a separator ahead of the registry-sourced tree.
  Reports which registry entry id (if any) was chosen.
* **`FileBrowserView`** keeps its existing `Qt::CustomContextMenu` policy and
  `itemContextMenuRequested(paths, globalPos)`/`folderContextMenuRequested(globalPos)` signals
  unchanged (right-click still selects an unselected row first, matching Explorer).
* **`WorkspacePaneWidget`**: on either signal, calls the matching `FileOperationsController` build
  method, constructs the fixed native `QAction`s (Open/Rename/Cut/Copy/Paste/Delete/New
  Folder+submenu/Properties — wired to the existing Cut/Copy/Paste/Delete/Open slots plus the new
  Rename/New-Folder/Properties ones), hands everything to `ContextMenuBuilder`, `exec()`s it at
  `globalPos`, then routes the result to `invokeContextMenuEntry` or `discardContextMenu`. New
  Folder immediately follows up with the Rename dialog so the user can name the new item — the
  practical equivalent of Explorer's inline rename-on-create without building inline edit support.
* **Mode toggle**: a single checkable `QAction` on `MainWindow`'s View menu ("Show shell extensions
  in context menu," off/static-only by default), persisted via `QSettings` (§9 precedent).
  `WorkspacePaneWidget` reads it at build-menu time and passes the resulting `ContextMenuSourceMode`
  straight into the build call — no Application- or adapter-layer plumbing carries the preference
  itself.
* **Refresh posture**: native actions emit `directoryContentsMayHaveChanged` via their existing
  (now also Rename/New-Folder/New-from-template) paths, same as before. For a registry/COM-invoked
  entry the app still can't know what it did to disk, so `invokeContextMenuEntry` keeps the old
  code's posture of emitting `directoryContentsMayHaveChanged` unconditionally on success.

#### 14.13.6 Threading and Not Built Here

* **Threading**: synchronous on the UI thread, same precedent as `ShellExecuteW`/`SHFileOperationW`
  (§14.11) and the "kept synchronous" note in §14.9 — building the entry list and invoking a chosen
  one are both quick, blocking calls; `QMenu::exec()` itself is what actually blocks while the
  popup is open, same as any other Qt modal popup.
* **Not built here**: a Linux implementation of `IContextMenuProvider` (stub failure, same posture
  as trash/open-with-default-app); a context menu triggered from drag-and-drop (no drag-and-drop in
  the app yet); inline in-grid rename-on-create (v1 uses a modal dialog instead, §14.13.4);
  `ShellNew`'s `Command`/binary-`Data` mechanisms (§14.13.3). A multi-selection context menu *is*
  built (§14.16): the Cut/Copy/Delete-equivalent entries act on every selected path; Rename/
  Properties/Open stay single-target and are disabled when more than one path is selected, since
  their backing APIs (`showProperties`, rename-as-move, `openFile`) are single-path.

### 14.14 Session Persistence (Window Geometry, Layout, Tabs, View & Sort)

Fulfills the §14.8 deferral: the split-window session — active `SplitLayout`, each pane's ordered
tabs (path, `ViewMode`, sort criterion/direction), the focused pane, and the main window's geometry
— now survives an app restart, backed by a JSON file rather than `QSettings` (§9). No Domain or
Application layer change; this is UI-session state, same posture as the rest of §14.

#### 14.14.1 New Components

* **`WorkspaceConfig.h`** (`src/adapters/viewmodels/`, new) — plain C++ (no Qt), alongside
  `SplitLayout.h`/`WorkspacePaneId.h`/`ViewMode.h`: `TabConfig` (path, `ViewMode`, `SortCriterion`,
  ascending flag), `PaneConfig` (ordered `TabConfig` list + active-tab index), `WorkspaceConfig`
  (`SplitLayout` + focused `WorkspacePaneId` + one `PaneConfig` per `WorkspacePaneId`).
* **`WorkspaceController`** (§14.3) gains `captureConfig() const -> WorkspaceConfig`, reading every
  pane's tabs via the existing `pane(id)/tabAt(i)` accessors, and `restoreFromConfig(const
  WorkspaceConfig&) -> bool`, which rebuilds each pane's tabs via the existing
  `WorkspacePaneViewModel::addTab()` + `TabViewModel::navigateTo/setViewMode/setSortCriterion`,
  then applies the saved active tab, `SplitLayout`, and focused pane. Returns whether any tab was
  restored, so `main.cpp` can fall back to the pre-existing "seed PaneA at the home directory"
  behavior on a fresh install. A saved path that no longer resolves degrades the same way any other
  navigation failure does today (`navigationFailed`, tab left on an empty listing) — no new error
  handling.
* **`WorkspacePaneViewModel`** gains a trivial `activeIndex() const` accessor for `captureConfig()`.
* **`src/adapters/config/`** (new adapter module, sibling to `persistence`/`filesystem`/`shell`):
  * `AppConfig.h` — `AppConfig { QByteArray windowGeometry; WorkspaceConfig workspace; }`. Unlike
    `WorkspaceConfig`, this type is allowed to reference Qt (`QByteArray`) directly — the module
    exists specifically as the Qt/JSON persistence boundary, the same posture `SQLiteTagRepository`
    and `ContextMenuIcon` already have at their own boundaries.
  * `AppConfigStore` — `load() -> AppConfig` / `save(const AppConfig&) -> bool`, backed by Qt's
    built-in `QJsonDocument`/`QJsonObject`/`QJsonArray` (`Qt6::Core` — already linked everywhere,
    no new `vcpkg.json` dependency). Reads/writes `%LOCALAPPDATA%/Exp-LORer/config.json` (Windows),
    same directory as `explorer.db` (§9), via a `configFilePath()` helper mirroring
    `CompositionRoot`'s existing `databasePath()`. Window geometry round-trips through
    `QMainWindow::saveGeometry()`/`restoreGeometry()` (base64-encoded in the JSON) rather than a
    hand-rolled x/y/width/height/maximized struct, since that already correctly handles maximized
    state, multi-monitor placement, and DPI. `SplitLayout`/`WorkspacePaneId`/`ViewMode`/
    `SortCriterion` values serialize as readable strings (e.g. `"TwoVertical"`), not raw ints, so
    the file stays hand-editable/debuggable and isn't brittle to enum reordering. `load()` is
    tolerant: a missing file, unparsable JSON, or any individual malformed/missing field all fall
    back to that field's default rather than failing the whole load; `qWarning()` (plain
    `Qt6::Core`, not a new dependency) surfaces problems without throwing. `save()` writes to
    `config.json.tmp` then renames over `config.json`, so a crash mid-write can't corrupt the
    previous good file. A top-level `"version"` field is reserved for future migrations; no
    migration framework exists yet.
* **`CompositionRoot`** owns the `AppConfigStore` (constructed with `configFilePath()`, same
  pattern as its `SQLiteTagRepository` member) and exposes it via `appConfigStore()`.
* **`MainWindow`** constructor takes an `AppConfigStore&` and the loaded `AppConfig`, applying
  `restoreGeometry()` (or a default size on first run); a new `closeEvent()` override captures
  `saveGeometry()` + `WorkspaceController::captureConfig()` into an `AppConfig` and calls
  `AppConfigStore::save()` before chaining to the base implementation — this is the only place a
  save happens (see §14.14.2).
* **`main.cpp`**: loads the config and calls `WorkspaceController::restoreFromConfig()` *before*
  constructing `MainWindow` (so `WorkspaceLayoutWidget`'s constructor-time `applyLayout()` already
  sees the restored layout/tabs), running the pre-existing home-directory seed only when nothing
  was restored.

#### 14.14.2 Save/Load Policy and Not Built Here

* **Save-on-close only**: the config is captured and written exactly once, from
  `MainWindow::closeEvent()`. No periodic/live autosave during a running session in this increment.
* **Not built here**: live/continuous autosave; migrating the existing `QSettings`-backed
  "show shell extensions in context menu" toggle (§14.13.5) into `config.json` — it's read via a
  static lookup at context-menu-build time today, and folding it into session state that only
  saves on close would need a live in-memory reference threaded through `WorkspacePaneWidget`,
  a separate change from this one; a config schema-migration framework beyond the reserved
  `"version"` field; persisting bookmarks/pinned folders or the thumbnail cache size limit (neither
  is implemented anywhere yet).

### 14.15 "This PC" Virtual Navigation Location

Adds a synthetic top-level navigation location — all local/remote drives, plus quick access to
Downloads/Documents/Pictures/Videos/Music/Desktop — reached from any drive's root directory via Up
(Specification.md §2.1). Modeled as a **sentinel path**, not a new location/tab type, so every
existing path-based mechanism (`TabViewModel::navigateTo`, `NavigationHistory`, `WorkspaceConfig`
session persistence, tab titles, tag-panel ancestor walking) keeps working unmodified.

* **Sentinel**: `VirtualPaths::ThisPC` (`src/application/VirtualPaths.h`, new — plain
  `std::filesystem::path`, no Qt/OS dependency, same posture as `NativeTypes.h`), literal value
  `L"this-pc:"`. A trailing `:` is not a legal Windows path-component character, so no real
  file/folder can ever collide with it.
* **No Port/interface change.** `IFileSystemRepository`/`FileNavigationUseCase` are untouched —
  both already pass `std::filesystem::path` through generically. Only the concrete
  `StandardFileSystemRepository` special-cases the sentinel in `listDirectory()`/`stat()`,
  preserving "all OS-specific code goes through `IFileSystemRepository`" (§6): a future Linux
  backend implements its own equivalent (mounts + XDG user dirs) behind the same sentinel, entirely
  inside its own adapter. Non-Windows returns an `IoError` "Not supported on this platform" failure
  for both entry points today, matching `openWithDefaultApplication`'s existing posture.
* **Drives** are synthesized as `FileNode`s whose `path()` is the real drive root (e.g. `C:\`), so
  double-click navigation, tagging, etc. work unmodified. Enumerated via `GetLogicalDrives()` +
  `GetDriveTypeW` (skipping `DRIVE_NO_ROOT_DIR`/`DRIVE_UNKNOWN`); the volume label comes from
  `GetVolumeInformationW` (tolerated failure — e.g. an empty optical drive — falls back to a
  type-based label rather than failing the whole listing). Since a root path's `.filename()` is
  empty (a `std::filesystem` quirk), the display label ("Local Disk (C:)", "Removable Disk (E:)",
  "DVD Drive (D:)", a volume label, ...) is carried via `FileNode::displayName()` instead — computed
  by the pure, hardware-free `DriveLabel::driveDisplayLabel(letter, driveType, volumeLabel)`
  (`src/adapters/filesystem/DriveLabel.h/.cpp`, new), split out specifically so it's unit-testable
  without touching real drives, the same rationale as `WorkspaceLayoutTopology::visiblePanes()`.
* **Quick access folders** are synthesized via `SHGetKnownFolderPath` (`FOLDERID_Downloads`,
  `FOLDERID_Documents`, `FOLDERID_Pictures`, `FOLDERID_Videos`, `FOLDERID_Music`,
  `FOLDERID_Desktop`; `CoTaskMemFree`d after use — `src/adapters/filesystem/CMakeLists.txt` gains
  `Ole32` for this, alongside the existing Windows-only `Shell32`). No `displayName` override
  needed — the real folder name ("Downloads", "Documents", ...) is already correct. A folder that
  fails to resolve is skipped rather than failing the whole listing.
* **`TabViewModel::goUp()`**: at `VirtualPaths::ThisPC`, no-op (top of the hierarchy). At a
  filesystem root (`parent_path() == currentPath()` — a local drive root or a UNC share root),
  navigates to `VirtualPaths::ThisPC` instead of the previous no-op — this is the "exits from any
  drive" trigger, and covers Backspace/Details-view ArrowLeft/the Up toolbar button for free since
  all of them funnel through `goUp()` already. `emitAvailability()`'s `upAvailable` simplifies to
  "has a current path and it isn't `VirtualPaths::ThisPC`".
* **Display text**: the address bar and tab title show "This PC" rather than the raw sentinel
  string (a small `displayPathText()` helper in `WorkspacePaneWidget`); typing "This PC"
  (case-insensitive) into the address bar navigates to the sentinel directly.
* **Tag panel**: `TagListViewModel::resolveTarget()` returns no target (rather than tagging the
  bare virtual root) when nothing is selected and the active tab's path is `VirtualPaths::ThisPC`.
  Tagging a drive or quick-access folder that *is* selected inside the This PC listing still works
  normally, since that resolves via `selectedEntries()`'s real path, not this fallback.
* **Background context menu**: right-clicking empty space while browsing This PC shows no menu
  (`WorkspacePaneWidget::showBackgroundContextMenu` returns early for the sentinel) rather than a
  New Folder/Paste/Properties menu that would just fail against it. Item-level right-click on a
  drive/quick-access row is unaffected.
* **Default landing location**: a fresh install (no saved `config.json`) and any newly-revealed
  empty pane now land on `VirtualPaths::ThisPC` instead of the user's home directory (`main.cpp`).
  `WorkspaceLayoutWidget`'s "newly-revealed empty pane" auto-seed and
  `WorkspacePaneWidget::onNewTabRequested` both already inherit the focused/active tab's *current*
  path, so this needed no change in either place — only `main.cpp`'s fallback changed.
* **Threading**: kept synchronous on the UI thread, consistent with every other
  `IFileSystemRepository` call today (§14.9's "kept synchronous" precedent; §5's background-dispatch
  model isn't implemented anywhere yet). `GetVolumeInformationW` on a removable/optical drive with
  no media inserted can add a brief delay before erroring — tolerated, not worked around.
* **Testing**: `DriveLabel::driveDisplayLabel()` and `FileNode::displayName()`/`withDisplayName()`
  are unit-tested (pure logic). `stat(VirtualPaths::ThisPC)` returning a synthetic `Directory` node
  is unit-tested. The live drive/known-folder enumeration itself is **not** covered by an automated
  test — the same carve-out §11 already documents for other live, per-machine OS-shell state
  (registry verbs, `ShellExecuteW`).
* **Not built here**: a Linux backend for the sentinel; drive free/total capacity display (a real
  Explorer parity pass needs `GetDiskFreeSpaceExW` plus a size-column rendering change);
  section/grouping headers ("Folders" vs. "Devices and drives" — This PC returns one flat,
  normally-sortable list); protecting quick-access folders from rename/delete beyond what the OS
  itself already refuses (deleting a real folder you can select is existing app behavior
  everywhere, not unique to this feature); async/background-thread dispatch for the enumeration; a
  dedicated `TabViewModelTest` (that class has no unit tests at all yet, a pre-existing gap, not
  introduced here — the new `goUp()` branching is verified by manual smoke test instead, per §11's
  UI-layer manual-testing convention).

### 14.16 Multi-Selection

Replaces the single-item selection §14.9/§14.10 explicitly deferred ("Selection stays single-item
for this increment ... multi-select is a natural but separate future increment") with real
Explorer-style multi-selection: rubber-band drag-select, Ctrl+click (toggle add/remove), Shift+click
(range add/remove), plus Ctrl+C/X/Delete and the right-click context menu acting on the whole
selection.

* **Selection mechanics come from Qt, not custom mouse handling.** `FileBrowserView` sets both
  `m_listView` and `m_treeView` to `QAbstractItemView::ExtendedSelection` (Ctrl/Shift-click
  semantics, plus rubber-band rectangle selection from empty space, are built into
  `QAbstractItemView` once the mode allows multi-select) and `QAbstractItemView::SelectRows` (so a
  click/drag anywhere in a `QTreeView` row selects the whole row across all Details-view columns,
  and `QItemSelectionModel::selectedRows()` reliably enumerates the full selection). The existing
  viewport event filter's empty-space "clicking empty space deselects" handling (§14.9's original
  commit) is extended to skip clearing when Ctrl or Shift is held, so a Ctrl+drag/Shift+drag
  rubber-band from empty space adds to the existing selection instead of wiping it first.
* **`TabViewModel::selectedEntries()`/`setSelectedEntries()`/`selectedEntriesChanged`**
  (`std::vector<FileNode>`) replace the single-item `selectedEntry()`/`setSelectedEntry()`/
  `selectedEntryChanged` from §14.9, same "owned by the tab, cleared on every navigation" posture.
  `FileBrowserView::selectionChanged(std::vector<FileNode>)` is built from
  `QItemSelectionModel::selectedRows()` (not `currentChanged`, which only reports the current row)
  and connected 1:1 to `setSelectedEntries`, same wiring shape as before.
* **Right-click on a row already part of the current selection** leaves the whole selection intact
  and passes every selected path to `itemContextMenuRequested`; right-click on a row *not* in the
  current selection still collapses to that one row first (§14.13's existing "right-click an
  unselected item selects it", unchanged).
* **Clipboard and delete act on the full selection**: `FileOperationsController::copyToClipboard`/
  `cutToClipboard` take a `std::vector<std::filesystem::path>` (§14.10); `WorkspacePaneWidget`'s
  delete handler and the context menu's Cut/Copy/Delete actions loop/pass the whole
  `selectedEntries()`/`paths` set instead of one item.
* **Single-target actions stay single-target.** Rename, Properties, and double-click/Enter Open are
  unaffected by this feature: their backing APIs (`FileNavigationUseCase::moveFile`,
  `IFileSystemRepository::showProperties`, `openWithDefaultApplication`) are single-path, so
  `WorkspacePaneWidget` disables Rename/Properties in the context menu's native action set whenever
  more than one path is selected, rather than guessing a batch behavior.
* **Tag panel requires exactly one selected item.** `TagListViewModel::resolveTarget()`: one
  selected → that entry (unchanged); zero selected → the browsed folder itself via `stat()`
  (unchanged, §14.9); more than one selected → no target, `addTagToSelection`/
  `removeTagFromSelection` report `operationFailed` instead. Multi-item tagging is not built.
* **Testing**: no automated coverage, same UI-layer manual-testing convention as §14.9/§14.15 — the
  selection view itself, `TabViewModel`, and `WorkspacePaneWidget` had no prior test files to extend
  either.
* **Not built here**: Ctrl+A select-all; a "N items selected" status-bar indicator; batch
  Rename/Properties for a multi-item selection; drag-to-move of a multi-selection (no drag-and-drop
  in the app yet, §14.13.6).

### 14.17 Tag Manager Dialog

Adds a standalone dialog for managing the **global** tag list — renaming a tag, deleting one outright
(cascading to every `FileTags` row via the schema's `ON DELETE CASCADE`, §7), or adding one without
also tagging a file/folder — as distinct from the tag panel (§14.9), which only attaches/detaches
tags to a specific selection. Opened via a new "Tag Edit..." action on the previously-empty Edit menu
(`MainWindow::createMenuBar()`). No Domain/Application/schema change: it is built entirely on
`TagManagementUseCase`'s pre-existing `createTag`/`updateTag`/`deleteTag`/`searchTags` methods (§2.2).

* **`TagManagerViewModel`** (`src/adapters/viewmodels/`, new) — `QObject`, same shape as
  `TagListViewModel`/`FileOperationsController` (owns a `TagManagementUseCase&`, no Qt/SQLite below
  it): `matchingTags()`/`setSearchQuery(QString)` (re-runs `TagManagementUseCase::searchTags`),
  `renameTag(Tag::Id, QString)` (rebuilds the `Tag` via `Tag::create(id, newName,
  existingTag.hexColor())` — name changes only, color untouched — then `updateTag`), `addTag(QString)`
  (color via the existing `TagColorPalette::pickColorFor`, same auto-pick
  `TagListViewModel::createAndAddTagFromQuery` already uses, then `createTag`), `deleteTag(Tag::Id)`.
  Each mutating slot re-runs the current search afterward. `operationFailed(QString)` signal on any
  `Result` failure, same pattern as every other ViewModel.
* **`TagManagerDialog`** (`src/ui/widgets/`, new) — `QDialog`: a `QLineEdit` search box wired to
  `setSearchQuery` on every keystroke, no debounce (same synchronous, no-debounce precedent as
  `TagListViewModel::setSearchQuery`); a `QListWidget` rebuilt wholesale on `matchingTagsChanged`
  (one row per tag: a solid-color square `QPixmap` from `tag.hexColor()` as icon, name as text,
  `Tag::Id` in `Qt::UserRole` — plain `QListWidgetItem`s rather than `TagChipWidget`, which is
  purpose-built for the panel's add/remove-button chip UX, not a selectable row list); Rename/Add/
  Delete/Close buttons, with Rename/Delete enabled only when exactly one row is selected. Add and
  Rename both prompt via a modal `QInputDialog::getText` (Add pre-filled with the trimmed search
  text if any; Rename pre-filled with the selected tag's current name) — reusing the exact
  rename-prompt convention §14.13.4 established for file rename. Delete prompts a `QMessageBox::
  question` confirmation (naming the tag and warning that its file associations are removed too)
  before calling `deleteTag`, mirroring §14.10's confirm-before-destructive-delete precedent.
  `operationFailed` surfaces via `QMessageBox::warning`.
* **Wiring**: `CompositionRoot` gains a trivial `tagManagementUseCase()` accessor (same shape as its
  existing `appConfigStore()` accessor) so `MainWindow` can reach the use case without widening
  `WorkspaceController`'s constructor. `MainWindow`'s constructor gains a `TagManagementUseCase&`
  parameter (passed from `main.cpp` alongside the existing `AppConfigStore&` argument); its Edit-menu
  action constructs a fresh `TagManagerViewModel` + `TagManagerDialog` per invocation and `exec()`s
  it — a one-shot modal, unlike the always-alive shared `TagListViewModel`.
* **Testing**: no automated coverage, same UI-layer manual-testing convention as §14.9/§14.15/§14.16
  — `TagManagementUseCase` itself (the only Application-layer code involved) is already fully covered
  by existing tests.
* **Not built here**: color editing (color stays auto-picked on Add, untouched on Rename — same
  posture as the tag panel); multi-select rename/delete; tag-usage counts in the list.

### 14.18 Filename Search (Per-Tab, Recursive)

Adds recursive filename/extension search scoped to a single tab's current folder
(Specification.md §2.1), triggered from a search box in the Layout toolbar and rendered *inside* the
tab it was run against, rather than as a separate panel or a new `SplitLayout`/`WorkspacePaneId`
value. Search state belongs to the `TabViewModel` it ran against, so it survives tab/pane switches
untouched and is never triggered implicitly for a different tab.

* **Port**: `IFileSystemRepository::listDirectoryRecursive(root)` (new, symmetric to the existing
  `listDirectory`) — `StandardFileSystemRepository` walks via `fs::recursive_directory_iterator` with
  `fs::directory_options::skip_permission_denied` (an unreadable subtree is skipped, not a whole-call
  failure, same posture as §14.15's tolerant enumeration). Rejects `VirtualPaths::ThisPC` outright
  (an error, not a listing) — recursing from the synthetic root would mean walking every local drive.
* **`FileNavigationUseCase`** gains `listDirectoryRecursive` (delegates to the Port) and a pure/static
  `filterByName(files, query)` — case-insensitive substring match against `FileNode::name()`, same
  shape and same local `toLower` helper as the existing `filterByExtension`.
* **`TabViewModel`** owns a *second* `FileListModel` (`searchResultsModel()`) alongside its normal
  one, plus `searchActive()`, the query text, and a cached full-recursive snapshot from the last scan.
  `startSearch(query)` (no-op on an empty/whitespace query) performs the one recursive Port call,
  filters + sorts (`SortCriterion::Name`, ascending) into `searchResultsModel()`, and flips
  `searchActive` on. `updateSearchQuery(query)` re-filters the cached snapshot only — no further disk
  I/O — which is what makes live-as-you-type filtering cheap. `exitSearch()` clears the state.
  `navigateTo()`/`goBack()`/`goForward()` (not `refresh()`) call `exitSearch()` unconditionally first,
  since resuming real navigation is incompatible with a stale search snapshot.
* **`WorkspacePaneWidget`** wraps each tab's page in a `QStackedWidget` of two `FileBrowserView`s —
  one bound to the tab's normal `fileListModel()`, one to its `searchResultsModel()` — reusing
  `FileBrowserView`/`FileListModel` as-is (icons, view modes, sorting, multi-selection, keyboard
  hotkeys, context menu all come for free) rather than introducing new view/model classes. The page
  connected to each `FileBrowserView` instance is wired identically (a shared private helper), so
  copy/cut/paste/delete/context-menu/navigation-hotkey behavior is unchanged whether the visible page
  is browsing results or search results. `TabViewModel::searchModeChanged` flips the stack's current
  page.
* **`MainWindow`** owns the one global search `QLineEdit`, placed in the existing Layout toolbar next
  to the layout `QAction`s. It always targets `WorkspaceController::focusedTab()` — the same hook
  already used to retarget the "Sort by" menu. `returnPressed` calls `startSearch`; `textEdited` calls
  `updateSearchQuery` while the focused tab is already in search mode (or `exitSearch` once the box is
  cleared); `onFocusedTabChanged` resyncs the box's text to whatever the newly-focused tab's search
  state is, so switching tabs never fires a search but does restore what the box should show.
* **Threading**: kept synchronous on the UI thread, same precedent as §14.15/§14.16/§14.17 (§5's
  background-dispatch model isn't implemented anywhere yet). A very large recursive folder will
  briefly freeze the UI during the initial Enter-triggered scan — accepted, not worked around.
* **Testing**: `FileNavigationUseCase::filterByName` (pure) and
  `StandardFileSystemRepository::listDirectoryRecursive` (real temp-directory tree, plus the
  `VirtualPaths::ThisPC` rejection) get GTest coverage, following the existing `filterByExtension`/
  `listDirectory` test style. `TabViewModel`'s new search state has no automated test, the same
  pre-existing gap §14.15/§14.16 already note ("`TabViewModel` ... has no unit tests at all yet"). UI
  layer: manual smoke test only, per §11's established convention.
* **Not built here**: a "Location" column to disambiguate same-named results from different
  subfolders (the Name column alone is ambiguous in that case); cancellation/background dispatch of
  the recursive scan; re-scanning disk as the query changes (the cached-snapshot filter can go stale
  relative to concurrent disk changes until the next Enter); extension-only query syntax (a plain
  substring match on the filename already covers it, e.g. typing `.txt`).