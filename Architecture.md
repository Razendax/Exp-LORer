Here is an architecture document for your C++ Advanced File Explorer, structured around the principles of Clean Architecture.

# Architecture Document

## 1. Architectural Approach

Project strictly adheres to **Clean Architecture** principles. The core philosophy is the **Dependency Rule**: source code dependencies must point inward toward the core business logic. External frameworks (Qt, SQLite, OS APIs) are treated as plugins to the core application.

This approach ensures the application is highly testable, independent of the UI framework, independent of the database, and resilient to changes in external libraries.


## 2. Layer Breakdown

The system is divided into four concentric layers, from the innermost core to the outermost mechanisms.

### 2.1 Domain Layer (Enterprise/Core Business Rules)

This layer contains the pure C++ plain data structures and core business logic. It has **no dependencies** on Qt, SQLite, or OS-specific APIs.

* `FileNode`: Entity representing a file or directory. Contains properties like `path`, `size`, `creationDate`, `modificationDate`, `fileType`, and `hash` (for tracking moves/renames).
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

### 2.3 Interface Adapters Layer

This layer converts data from the format most convenient for the use cases and entities to the format most convenient for the external frameworks (like Qt Views or SQLite).

**Controllers / ViewModels (Qt Models):**
* `FileListModel`: a single `QAbstractTableModel` (columns: Name, Size, Type, Date modified) wrapping directory-listing results (`FileNode` lists) supplied by `NavigationViewModel`. One model feeds both `QListView` (icon/list/tiles view modes, which only read column 0) and `QTreeView` (details view mode, all columns) — see §2.3.1. This supersedes an earlier sketch of two separate `FileTreeViewModel`/`FileGridViewModel` types: a single shared model matches how Explorer itself works and avoids duplicating file→row mapping logic.
* `ViewMode` (`src/adapters/viewmodels/ViewMode.h`): plain enum — `ExtraLargeIcons`, `LargeIcons`, `MediumIcons`, `SmallIcons`, `List`, `Details`, `Tiles`. Presentation-only state, currently held on `NavigationViewModel` (see §2.3.1, §14.1).
* `TagListViewModel`: `QObject`-derived, single shared instance owned/retargeted by `WorkspaceController` (see §14.9) to whichever pane/tab has focus. Exposes three sections for the tag panel: `folderTags()` (the focused tab's current folder plus every ancestor's tags, via `TagManagementUseCase::tagsForPathWithAncestors`), `searchResults()` (live-filtered via `TagManagementUseCase::searchTags` as the user types, excluding tags already on the current target), and `selectedItemTags()` (tags of the focused tab's `selectedEntry()`, or the current folder itself when nothing is selected). Slots `addTagToSelection`/`removeTagFromSelection`/`createAndAddTagFromQuery` drive `TagManagementUseCase`.
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
| **Workspace/Session** | `WorkspaceController`, `WorkspacePaneViewModel`, `TabViewModel`, `FileOperationsController` | UI-session state for the fixed 4-pane split-window layout (see §14); no business rules, no Domain/Application changes. `FileOperationsController` (see §14.10) drives Ctrl+C/X/V/Delete hotkeys via the real OS clipboard. |

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
* **User settings** (window geometry, bookmarks/pinned folders, last directory, thumbnail cache size limit): stored via `QSettings` (INI format for cross-platform consistency rather than the Windows registry).
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
│   │   └── media/                # QtMediaDecoder / FFmpegMediaDecoder, ThumbnailGenerator
│   ├── ui/
│   │   ├── widgets/               # MainWindow, FileBrowserView, FileTileDelegate, TagPanelWidget,
│   │   │                          # TagChipWidget, dialogs (Qt Widgets, .ui files),
│   │   │                          # WorkspaceLayoutWidget, WorkspacePaneWidget (see §14, §14.9)
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

* **Session persistence** (restoring `SplitLayout` + each pane's tabs/paths/view modes across
  restarts) is explicitly out of scope for this increment. A future `WorkspaceSessionStore`
  (QSettings-backed, mirroring §9's window-geometry handling) is the natural place for it; it would
  need a serializable snapshot type capturing `SplitLayout` + each `WorkspacePaneViewModel`'s tab
  paths/view-modes + focused pane — not designed further here.
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

* **Selection is per-tab state.** `TabViewModel::selectedEntry()`/`setSelectedEntry()`/
  `selectedEntryChanged` mirror `currentPath`/`currentPathChanged`: owned by the tab, cleared on
  every `navigateTo`/`goBack`/`goForward`/`goUp` so a stale selection from the previous folder
  never leaks into the new one. `FileBrowserView` shares one `QItemSelectionModel` between its
  `QListView` and `QTreeView` (so switching `ViewMode` doesn't drop the selection) and emits
  `selectionChanged(std::optional<FileNode>)`, which `WorkspacePaneWidget` connects 1:1 to each
  page's own `TabViewModel::setSelectedEntry` at tab-creation time (unlike the toolbar, which
  rebinds to whichever tab is active, selection wiring doesn't need to rebind).
* **`TagListViewModel::setActiveTab(TabViewModel*)`** is `WorkspaceController`'s retargeting slot,
  invoked whenever `focusedPaneChanged` or any pane's `activeTabChanged` fires (and once at
  construction): it disconnects the previous tab's `currentPathChanged`/`selectedEntryChanged`,
  connects the new one, and refreshes all three panel sections immediately.
* **Resolving the tagging target** (used for the panel's "+"/"x" actions and the
  selected-item-tags section): `selectedEntry()` if set, otherwise the current folder itself —
  fetched via a new `IFileSystemRepository::stat`/`FileNavigationUseCase::stat` single-path
  lookup (distinct from `listDirectory`, which enumerates children, not the directory itself).
  This is what lets a user tag the folder they're browsing without selecting a child row first.
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
* **Selection stays single-item** for this increment (Ctrl+C/X/Delete act on
  `TabViewModel::selectedEntry()`); multi-select is a natural but separate future increment.

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