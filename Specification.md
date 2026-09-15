
# Requirements Document

## 1. Project Overview

### 1.1 Purpose

The purpose of this project is to develop a custom desktop file explorer in C++ that goes beyond traditional file management. It integrates a database-backed tagging system for advanced file organization and includes a built-in media viewer (images and videos) to provide a seamless browsing and preview experience without needing external applications.

### 1.2 Target Platform

* **Operating System:** Cross-platform, delivered in phases — Windows first (primary development and QA target for v1), with Linux support following once the Windows build is stable. All OS-specific code goes through an abstraction layer so a Linux backend can be added later without touching core logic.
* **Language:** C++20.
* **UI Framework:** Qt 6 (Qt Widgets for the main application shell; Qt Quick/QML embedded for the media viewer).


## 2. Functional Requirements

### 2.1 Standard File Explorer Functionalities

**Directory Navigation:**
* Tree view and list/grid view of the local file system.
* Standard navigation controls (Back, Forward, Up, Path Bar).
* Ability to bookmark or pin frequently used folders.
* Support for long paths (beyond the traditional 260-character Windows limit) and Unicode file/folder names throughout navigation, search, and tagging.
* **Split-window layouts:** The window arranges into one of four fixed layouts — single pane, two-pane vertical split, two-pane horizontal split, or a four-pane grid — chosen from the View menu/toolbar. No arbitrary or recursive nesting.
* **Multi-tab browsing, per pane:** Each pane independently owns its own tabs (add/close/switch), each retaining its own navigation history (back/forward stack), current path, view mode, and sort order; a pane's tabs are unaffected by other panes or by switching layouts (a pane's tabs persist even while hidden by a layout that shows fewer panes). The tag filter panel and media preview are single shared instances that follow whichever pane/tab currently has focus, rather than existing per-pane. Session restore across restarts is deferred future work (see Architecture.md §14.8), not part of this increment.
* **Sort order:** Directory contents can be sorted by Name, Size, Type, or Date modified, ascending or descending, via a "Sort by" submenu under the View menu (Architecture.md §14.12) or by clicking a column header in Details view. The chosen sort applies uniformly across all seven view modes and is preserved per tab across navigation within that tab; a newly opened tab starts at the default (Name, ascending) rather than inheriting another tab's choice.

**File Operations:**
* Create, open, rename, delete (move to trash/permanently delete), and copy/paste files and folders.
* Double-clicking or pressing Enter/Return on a file opens it with the OS-registered default
  application; folders navigate into them the same way (Architecture.md §14.11).
* File properties inspection (size, creation date, modification date, file type).
* **Keyboard hotkeys** (single selected file/folder at a time in v1): Ctrl+C copy, Ctrl+X cut,
  Ctrl+V paste, Delete moves to the Recycle Bin, Shift+Delete deletes permanently, Backspace goes
  to the parent folder. Typing a letter `[a-z]` selects the next item whose name starts with that
  letter, cycling through matches on repeated presses. In Details view only, ArrowLeft also goes to
  the parent folder and ArrowRight opens a selected folder (no effect on a selected file). Ctrl+C/
  X/V read and write the real OS clipboard, so cut/copy/paste interoperate with File Explorer and
  other applications, not just within Exp-LORer.

**Search and Filter:**
* Fast filename and extension search within the current directory and subdirectories.

### 2.2 Database-Backed File Tagging System

**Tag Management:**
* Users can create, edit, delete, and color-code custom tags (e.g., "Work", "Urgent", "ProjectA", "Favorite").

**Tag Assignment:**
* Ability to attach multiple tags to any file or folder.
* Context-menu integration and drag-and-drop tagging.

**Database Backend:**
* Local lightweight database (e.g., SQLite) to persist tag metadata, associating file paths (or unique file hashes) with tag IDs.


**Tag-Based Filtering & Search:**
* Dedicated view/panel to filter files by one or multiple tags (e.g., show all files tagged with "Work" *AND* "ProjectA").
* Robust handling of file path changes (handling renames/moves gracefully by tracking file hashes or updating paths in the database).

**Tag Panel:**
* A panel sharing the window with the workspace pane area shows, for whichever pane/tab currently
  has focus: (1) the tags of the active pane's current folder together with every ancestor
  folder's tags (ancestors listed before the folder's own tags); (2) a search box over all tags;
  (3) the tags matching that search, live-updating as the user types, each addable via a "+" to
  the currently selected file/folder in the active pane; (4) the tags of only that selected item
  (not its ancestors), each removable via an "x". Folder tags are not inherited by files/folders
  nested inside them — they only ever apply to the folder they were assigned to.
* When no item is selected in the active pane, the panel's add/remove sections (3 and 4) target
  the currently browsed folder itself, so a folder can be tagged without first navigating into it.

### 2.3 Built-in Image Viewer

**Supported Formats:** Common image formats including JPEG, PNG, BMP, GIF, and WebP.

**Viewing Capabilities:**
* Instant preview pane when a file is selected, as well as a full-window view mode.
* Zoom in/out, pan, and rotate (90-degree increments).
* Slide-show mode for browsing all images within the current directory.
* Direct tag assignment from the image viewer interface.


### 2.4 Built-in Video Player

**Supported Formats:** Common video containers and codecs (e.g., MP4, MKV, AVI, MOV) dependent on system codecs or integrated libraries (like FFmpeg or Qt Multimedia).

**Playback Controls:**
* Play, pause, seek bar (scrubbing), volume control, and mute.
* Fullscreen toggle and aspect ratio adjustments.
* Loop playback option.


**Previewing:**
* Thumbnail generation for video files in the grid/list view.



## 3. Non-Functional Requirements

### 3.1 Performance & Resource Management

* **Responsiveness:** The UI must remain responsive during heavy file system scans or database queries by utilizing multi-threading (asynchronous directory loading and database operations).
* **Memory Efficiency:** Smooth image caching and video buffer management to prevent high RAM consumption when browsing large directories of media files.
* **Performance Targets (v1, loose/indicative — not hard SLAs):** these are sanity-check targets on typical development hardware (NVMe SSD, local directories), not guarantees under network drives or spinning disks:
  * Directory listing (~10,000 entries, local disk): first visible results within ~500ms; full listing settled within ~2s.
  * Tag filter query (~100,000 tagged files): results within ~500ms.
  * Application cold start to usable window: under ~2s.
  * These are intentionally loose for v1 and may be tightened once real profiling data exists; they exist to catch gross regressions, not to drive premature optimization.

### 3.2 Reliability and Data Integrity

* **Database Consistency:** The SQLite database must handle concurrent reads/writes safely. Transactions should be used when bulk-tagging files.
* **Single Instance:** Running multiple instances of the application simultaneously is not supported/allowed. Launching a second instance while one is already running must detect the existing instance and activate its window instead of starting a new process, to avoid concurrent writers to the same database and thumbnail cache.
* **Error Handling:** Graceful handling of permission errors, missing files, or corrupted media formats without crashing the application.
* **Logging & Diagnostics:** Application events, warnings, and errors must be logged to a rotating on-disk log file (plus console output in debug builds) to aid troubleshooting without requiring a debugger.


## 4. Proposed Technology Stack

* **Build System:** CMake, with **vcpkg** (manifest mode, `vcpkg.json`) managing all third-party dependencies and a `CMakePresets.json` pinning the toolchain.
* **Core Language:** C++20
* **Testing Framework:** Google Test (gtest) + Google Mock (gmock) for unit and integration testing.
* **UI & Framework:** Qt 6 — Qt Widgets (main application shell) + Qt Quick/QML (embedded media viewer).
* **Database:** **SQLite** (embedded, lightweight, zero-configuration file database).
* **Media Processing (Alternative/Addition):** **FFmpeg libraries** (libavcodec, libavformat) if deeper control over video decoding and custom rendering is required, and/or Qt Multimedia for standard playback.
* **Logging:** **spdlog**, with a rotating file sink for on-disk logs and a console sink in debug builds.

## 5. V1 Scope Exclusions

The following are explicitly out of scope for the initial release (v1), pending future requirements revision:

* Undo/redo for file operations (delete/move/rename) and tag assignment.
* Linux-specific trash integration — Windows trash ships first; Linux falls back to permanent delete until implemented.
* Automated UI testing — manual smoke testing only for v1.
* Internationalization/localization — UI strings are English-only for v1.

