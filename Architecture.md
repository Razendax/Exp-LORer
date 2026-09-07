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
* `FileTreeViewModel` / `FileGridViewModel`: Wraps the `FileNavigationUseCase` to expose data to Qt's View architecture (e.g., inheriting from `QAbstractListModel` or `QAbstractItemModel`).
* `TagListViewModel`: Exposes tag data for the UI filter panel.
* `MediaPreviewViewModel`: Manages state for the image/video preview panes.


**Repository Implementations:**
* `SQLiteTagRepository`: Implements `ITagRepository`. Maps SQL queries and `sqlite3` (or `QtSql`) results to `Tag` entities.
* `StandardFileSystemRepository`: Implements `IFileSystemRepository` using C++17/20 `<filesystem>` (`std::filesystem`).
* `QtMediaDecoder` / `FFmpegMediaDecoder`: Implements `IMediaDecoder` to generate thumbnails and provide playback streams.


### 2.4 Frameworks & Drivers Layer

The outermost layer composed of the actual UI, database engine, and external libraries.

* **UI Framework (Qt 6):** QML or Qt Widgets handling drawing, user input, drag-and-drop, and window management.
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

---

## 5. Concurrency and Threading Model

To satisfy the non-functional requirement for responsiveness, the architecture enforces strict threading rules:

* **Main Thread (UI Thread):** Strictly reserved for the Frameworks Layer (Qt Event Loop) and ViewModels. No direct disk or database I/O is permitted here.
* **Worker Threads (Background Tasks):** * **I/O Thread Pool:** Managed by the Application Layer. Operations invoked on `IFileSystemRepository` (directory scanning, large file copying) and `ITagRepository` (database queries) are dispatched to background threads (using `std::async`, C++20 coroutines, or `QThreadPool`).
* **Media Thread:** Dedicated thread for `IMediaDecoder` to decode video frames or generate image thumbnails without stuttering the UI.


* **Data Synchronization:** Callbacks or Qt Signals/Slots (using queued connections) are used to safely pass `FileNode` entities or playback frames from worker threads back to the Main Thread ViewModels.
* **Database Concurrency:** The `SQLiteTagRepository` will configure SQLite in Serialized mode (Thread-safe) or utilize a single dedicated database thread to prevent `SQLITE_BUSY` or locking issues during concurrent reads (searching tags) and writes (bulk tagging).