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
* `TagManagementUseCase`: Handles creating, updating, deleting, and assigning tags to files. Resolves broken paths by checking file hashes.
* `MediaProcessingUseCase`: Manages the extraction of thumbnails and basic metadata for images and videos.

**Gateway Interfaces (Ports):**

* `IFileSystemRepository`: Interface for OS file operations (list, move, delete, copy, hash generation).
* `ITagRepository`: Interface for persisting tags and associations.
* `IMediaDecoder`: Interface for decoding media streams and extracting thumbnails.

### 2.3 Interface Adapters Layer

This layer converts data from the format most convenient for the use cases and entities to the format most convenient for the external frameworks (like Qt Views or SQLite).

**Controllers / ViewModels (Qt Models):**
* `FileTreeViewModel` / `FileGridViewModel`: Wraps the `FileNavigationUseCase` to expose data to Qt's View architecture (e.g., inheriting from `QAbstractListModel` or `QAbstractItemModel`), consumed by Qt Widgets views (`QTreeView`/`QListView`).
* `TagListViewModel`: Exposes tag data for the UI filter panel (Qt Widgets).
* `MediaPreviewViewModel`: `QObject`-derived, `Q_PROPERTY`-exposed state for the image/video viewer, consumed by a QML scene embedded via `QQuickWidget` inside the main Widgets window.


**Repository Implementations:**
* `SQLiteTagRepository`: Implements `ITagRepository`. Maps SQL queries and `sqlite3` (or `QtSql`) results to `Tag` entities.
* `StandardFileSystemRepository`: Implements `IFileSystemRepository` using C++17/20 `<filesystem>` (`std::filesystem`). On Windows, paths are handled as UTF-16 (`std::filesystem::path` natively) and long paths (>260 chars) are supported via the `\\?\` extended-length prefix; paths are converted to UTF-8 only at the SQLite/adapter boundary (see §6).
* `QtMediaDecoder` / `FFmpegMediaDecoder`: Implements `IMediaDecoder` to generate thumbnails and provide playback streams.


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
2. **Adapter (ViewModel):** UI invokes `loadDirectory(path)` on the `FileTreeViewModel`.
3. **Application (Use Case):** ViewModel calls `execute(path)` on the `FileNavigationUseCase`.
4. **Application -> Adapter (Port/Repo):** The Use Case asks the `IFileSystemRepository` to fetch the files.
5. **Adapter -> Framework (OS):** `StandardFileSystemRepository` uses `std::filesystem::directory_iterator` to read the disk and returns a list of `FileNode` entities.
6. **Application -> Adapter:** The Use Case passes the `FileNode` list back to the `FileTreeViewModel`.
7. **Adapter -> UI:** The ViewModel formats the entities into Qt roles (DisplayRole, DecorationRole) and emits `dataChanged` signals, updating the UI.

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
| **Presentation** | `MainView`, `TagFilterPanel`, `MediaPreviewPane` | Qt-based UI elements binding to ViewModels. |
| **Workspace/Session** | `WorkspaceController`, `TabViewModel`, `PaneViewModel`, `SplitPaneNode` | UI-session state for multi-tab/split-screen browsing (see §14); no business rules, no Domain/Application changes. |

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
│   │   ├── viewmodels/           # FileTreeViewModel, TagListViewModel, MediaPreviewViewModel,
│   │   │                         # NavigationHistory, SplitPaneNode, PaneViewModel, TabViewModel,
│   │   │                         # WorkspaceController, WorkspaceLayoutSnapshot (see §14)
│   │   ├── persistence/          # SQLiteTagRepository, migration scripts, WorkspaceSessionStore (see §14)
│   │   ├── filesystem/           # StandardFileSystemRepository (std::filesystem + OS trash calls)
│   │   └── media/                # QtMediaDecoder / FFmpegMediaDecoder, ThumbnailGenerator
│   ├── ui/
│   │   ├── widgets/               # MainWindow, TagFilterPanel, dialogs (Qt Widgets, .ui files),
│   │   │                          # SplitPaneWidget, FileBrowserPaneWidget (see §14)
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

## 14. Multi-Tab / Split-Screen Extension

This section extends the architecture to support multiple tabs, each containing an independent
split-pane tree, alongside the existing navigation/tagging/media-viewer design. It does not modify
the Domain or Application layers.

### 14.1 Scope and Product Decisions

Tabs contain independent split-pane trees (not the reverse — split panes do not contain tabs). The
tag filter panel and the media preview are each a single shared instance, retargeted to whichever
pane currently has focus, rather than duplicated per pane. The media viewer is a docked preview pane
bound to the active pane's selection; full-window mode expands it over the whole `MainWindow` rather
than opening a second window. View mode (tree/grid) is scoped per-pane.

### 14.2 Key Architectural Decisions

* **Domain and Application layers are unchanged.** Tabs, panes, and workspaces are pure UI-session
  state with no business rules — no new entities, no new Ports, no new use cases.
* **`FileNavigationUseCase`, `TagManagementUseCase`, and `MediaProcessingUseCase` must remain
  stateless** orchestrators over their existing Ports. One shared instance of each serves every pane
  concurrently; only ViewModels are instantiated per-pane.
* **Exactly one `MainWindow`.** Tabs and splits render inside its single central widget; no pane/tab
  component may ever construct a second top-level window (preserves the existing
  single-instance-foreground assumption from §6/§9).
* **Tabs own split-pane trees.** Each `TabViewModel` owns one independent `SplitPaneNode` tree;
  splitting inside a tab never affects other tabs.
* **Tag filter panel and media preview are single shared instances**, retargeted to whichever pane
  currently has focus — not duplicated per pane.

### 14.3 New Components by Layer

**`src/domain/`, `src/application/`** — no changes. Existing use cases/Ports are reused as-is,
constrained to stay stateless so `CompositionRoot` can safely share one instance of each across N
panes.

**`src/adapters/viewmodels/`** (new files)
* `NavigationHistory.h/.cpp` — per-pane back/forward stack (Command pattern: `NavigateToPathCommand`
  objects pushed/popped). Plain C++, no Qt.
* `SplitPaneNode.h/.cpp` — Composite: abstract `SplitPaneNode`; leaf `PaneNode` (holds a `PaneId`);
  composite `SplitContainerNode` (orientation + ordered children + size ratios). Plain C++ tree, no
  `QWidget`/`QObject` — testable/serializable independent of the widget tree.
* `PaneViewModel.h/.cpp` — `QObject`; owns one `FileTreeViewModel`/`FileGridViewModel` + one
  `NavigationHistory` + a `PaneId` + its own view-mode state; exposes
  `navigateTo/goBack/goForward/goUp`; emits `directoryChanged`, `activated`, `selectionChanged`.
* `TabViewModel.h/.cpp` — `QObject`; owns the root `SplitPaneNode` for one tab, a title, and
  `activePaneId`; emits `titleChanged`, `layoutChanged`, `activePaneChanged`.
* `WorkspaceController.h/.cpp` — the Mediator. Owns the ordered `TabViewModel` list + active-tab
  index; exposes `addTab()/closeTab()/splitPane()/closePane()/activePane()`; retargets the shared
  `TagListViewModel`/`MediaPreviewViewModel` to the active pane on focus change; produces/consumes a
  `WorkspaceLayoutSnapshot`.
* `WorkspaceLayoutSnapshot.h` — plain struct (Memento): recursive split shape + each leaf's
  path/view-mode + active tab/pane indices. Uses `std::filesystem::path`, no Qt types —
  QSettings/JSON conversion happens only at the adapter boundary that persists it.

**`src/adapters/persistence/`** (new file)
* `WorkspaceSessionStore.h/.cpp` — reads/writes `WorkspaceLayoutSnapshot` via `QSettings`, mirroring
  the existing window-geometry handling (§9). Deliberately **not** a new Port/use case: it is
  UI-session persistence, not business data, so it does not go through `ITagRepository`/SQLite.

**`src/ui/widgets/`**
* `MainWindow.h/.cpp` (modified) — hosts a tab strip (`QTabWidget`) of `SplitPaneWidget`s built from
  a `WorkspaceController` handed in by `CompositionRoot`; gains New Tab/Close Tab/Split/Close-Pane
  actions; hosts the docked media-preview widget and `TagFilterPanel`, each bound to the one shared
  ViewModel instance.
* `SplitPaneWidget.h/.cpp` (new) — recursively builds nested `QSplitter`s from a `TabViewModel`'s
  `SplitPaneNode` tree.
* `FileBrowserPaneWidget.h/.cpp` (new) — leaf widget: `QTreeView`/`QListView` bound to its
  `PaneViewModel`'s `FileTreeViewModel`/`FileGridViewModel`, plus a per-pane breadcrumb/back/forward
  toolbar.
* `TagFilterPanel` — existing planned type, unchanged; single dock widget bound to the one shared
  `TagListViewModel`.

**`src/app/CompositionRoot.h/.cpp`** (modified)
* Keeps single shared adapter instances (`StandardFileSystemRepository`, `SQLiteTagRepository`,
  media decoder, shared thread pool) and single shared use-case instances.
* Gains Factory Methods: `createPaneViewModel(viewMode)`, `createTabViewModel()`,
  `createWorkspaceController()` — each wires a fresh ViewModel to the shared use cases. No DI
  framework/Abstract Factory introduced — stays consistent with "single composition root,
  constructor injection only" (§6).

### 14.4 Component Relationship

```
MainWindow (single top-level window)
 └── WorkspaceController (Mediator)
      ├── TabViewModel #1
      │     └── SplitPaneNode tree
      │           └── SplitContainerNode
      │                 ├── PaneNode → PaneViewModel A → FileTreeViewModel A ─┐
      │                 └── PaneNode → PaneViewModel B → FileGridViewModel B ─┤
      │           (each PaneViewModel owns its own NavigationHistory)         │
      ├── TabViewModel #2 (single unsplit PaneNode)                          │
      ├── TagListViewModel (shared, 1 instance) ◄── retargeted to active pane
      └── MediaPreviewViewModel (shared, 1 instance) ◄── retargeted to active pane
                                                                              │
                                                                              ▼
                                                   FileNavigationUseCase (shared, stateless)
                                                                              │
                                                                              ▼
                                                   IFileSystemRepository (shared, CompositionRoot)
                                                   → shared thread pool → queued signal back to
                                                     the originating FileTreeViewModel/Grid only
```

### 14.5 SOLID / Pattern Rationale

* **SRP**: `PaneViewModel` (display/query orchestration) vs. `NavigationHistory` (where you've been)
  vs. `TabViewModel` (tab metadata + active-pane bookkeeping) vs. `WorkspaceController`
  (cross-pane/tab mediation) are four distinct reasons to change.
* **OCP**: new pane flavors are added via new `CompositionRoot` factory methods, not by widening
  `PaneViewModel`'s constructor contract.
* **LSP**: `PaneNode`/`SplitContainerNode` are interchangeable to any tree-walker (renderer,
  serializer) — required for the Composite to work.
* **ISP**: `IFileSystemRepository`/`ITagRepository`/`IMediaDecoder` get no pane-aware methods — panes
  are presentation-only and must not leak into Ports built around `FileNode`/`Tag`.
* **DIP**: `PaneViewModel` never references a concrete sibling `PaneViewModel` or
  `WorkspaceController` directly; it only emits Qt signals that `WorkspaceController` subscribes to
  (Observer realizing the Mediator).

**Patterns used**: Composite (`SplitPaneNode` tree), Factory Method
(`CompositionRoot::createXViewModel()`), Mediator (`WorkspaceController`), Observer (Qt
signals/slots, the codebase's existing idiom), Command (`NavigationHistory`'s back/forward stacks),
Memento (`WorkspaceLayoutSnapshot`).

### 14.6 Usage Rules

**Must:**
* `FileNavigationUseCase`/`TagManagementUseCase`/`MediaProcessingUseCase` stay stateless so one
  shared instance safely serves concurrent calls from N panes.
* Cross-pane/tab coordination goes through `WorkspaceController`; `PaneViewModel` communicates only
  via its own signals.
* `PaneViewModel`/`TabViewModel`/`WorkspaceController` are created only via `CompositionRoot`'s
  factory methods — UI widgets request new panes/tabs through `WorkspaceController`'s API, never
  `new PaneViewModel(...)` directly.
* `SplitPaneNode` and `WorkspaceLayoutSnapshot` stay plain C++ (no `QWidget`/`QObject` embedded); the
  `QSplitter` tree is built *from* them, separately.
* All async pane work flows through the existing shared repository/decoder instances and their
  shared thread pool — no ad hoc per-pane threads.
* `WorkspaceLayoutSnapshot` persistence goes through `QSettings` only, never `ITagRepository`/SQLite.
* Exactly one `MainWindow` per process; tabs/splits render only inside it.
* Focus changes (pane activation) retarget the shared `TagListViewModel`/`MediaPreviewViewModel`
  through `WorkspaceController` — no pane holds its own copy of either.

**Must not:**
* No `PaneId`/`TabId`/`SplitPaneNode` type may appear in any `src/domain` or `src/application`
  header.
* No pane/tab/workspace component may create its own `QThread`/`QThreadPool`/`std::thread`.
* `NavigationHistory` must not perform disk I/O itself — only bookkeeping; validity is re-checked
  when `navigateTo` re-runs through `FileNavigationUseCase`.

### 14.7 Navigation History, Session Persistence, Threading

* **History is per-pane**: each `PaneViewModel` owns exactly one `NavigationHistory`; closing a pane
  discards it; there is no global back/forward.
* **Session persistence** plugs in at `WorkspaceSessionStore` (QSettings-backed): captures tab titles
  + split shape + each leaf's current path + view mode; restored at startup by replaying
  `WorkspaceController::restore(snapshot)`, which calls `CompositionRoot::createPaneViewModel()` per
  leaf then `navigateTo(path)`. Back/forward history stacks are **not** persisted in v1 (rebuilt
  empty on restore) — deliberate minimalism.
* **Threading**: simultaneous navigations from different panes become independent tasks on the same
  shared `IFileSystemRepository`-owned pool; results marshal back via queued signals to the
  *originating* `FileTreeViewModel`/`Grid` only (each is a distinct `QObject`) — no cross-pane bleed,
  no per-pane thread pools.

### 14.8 Deferred UX Policy (not architecture-blocking; default stated for now)

* Max pane count / split nesting depth: architecturally unbounded (Composite recurses); suggest a UX
  cap (~4–6 panes, depth 2) to be enforced in `WorkspaceController::splitPane()`, not in
  `SplitPaneNode` itself.
* Closing the last tab: disable "Close Tab" when exactly one tab remains, rather than closing the
  window or showing an empty state.
* Cross-pane drag-and-drop (dragging a file between panes to copy/move): out of scope for this
  extension; if added later, `WorkspaceController` is the natural broker — not assumed here.