
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
* **Path bar folder autocomplete** (Architecture.md §14.20): as the user types, a popup lists only
  the matching folders (never files) under the implied parent directory, navigable with the
  keyboard or mouse, with inline autocompletion of an unambiguous match.
* **"This PC" virtual location** (Architecture.md §14.15): lists all local and remote drives, plus
  quick access to the Downloads, Documents, Pictures, Videos, Music, and Desktop folders. Reached
  by pressing Up (or Backspace) from any drive's root directory — the point where a regular folder
  hierarchy "exits" onto the drive listing — or by typing "This PC" into the address bar. This is
  also the default landing location for a fresh install (see below) and for any newly-revealed
  empty pane.
* Ability to bookmark or pin frequently used folders.
* Support for long paths (beyond the traditional 260-character Windows limit) and Unicode file/folder names throughout navigation, search, and tagging.
* **Split-window layouts:** The window arranges into one of four fixed layouts — single pane, two-pane vertical split, two-pane horizontal split, or a four-pane grid — chosen from the View menu/toolbar. No arbitrary or recursive nesting.
* **Multi-tab browsing, per pane:** Each pane independently owns its own tabs (add/close/switch), each retaining its own navigation history (back/forward stack), current path, view mode, and sort order; a pane's tabs are unaffected by other panes or by switching layouts (a pane's tabs persist even while hidden by a layout that shows fewer panes). The tag filter panel and media preview are single shared instances that follow whichever pane/tab currently has focus, rather than existing per-pane. **Session persistence:** the active layout, each pane's tabs (path, view mode, sort order), the focused pane, and the main window's geometry are saved to a local JSON config file when the app closes and restored the next time it starts (Architecture.md §14.14); a fresh install with no saved config falls back to a single pane opened at the "This PC" virtual location (Architecture.md §14.15), rather than the user's home directory.
* **Sort order:** Directory contents can be sorted by Name, Size, Type, or Date modified, ascending or descending, via a "Sort by" submenu under the View menu (Architecture.md §14.12) or by clicking a column header in Details view. The chosen sort applies uniformly across all seven view modes and is preserved per tab across navigation within that tab; a newly opened tab starts at the default (Name, ascending) rather than inheriting another tab's choice.
* **Hidden files/folders:** A "Show hidden files" toggle under the View menu (Architecture.md
  §14.21) controls visibility of OS-hidden entries app-wide and persists across restarts. When on,
  hidden files/folders appear in every view mode and in search results with grayed-out text so
  they're visually distinguishable; when off, they're excluded from directory listings, search
  results, and address-bar autocomplete entirely.
* **Integrated terminal (Architecture.md §14.23):** Each tab has its own collapsible panel at the
  bottom, collapsed by default, showing a strip of panel tabs (only "Terminal" in v1). Clicking
  "Terminal" the first time expands the panel and opens a `cmd.exe` session already in that tab's
  current folder; collapsing and re-expanding the panel leaves that session running. Closing the tab
  ends its terminal session.
* **Settings dialog (Architecture.md §14.26):** `File > Settings...` opens a dialog with a
  hierarchical category tree on the left and that category's settings on the right; in v1 this
  holds two categories: Highlight > Languages, listing the languages with syntax highlighting
  (2.2 "Preview Panel") and, per language, each of its token types with a color swatch the user can
  change via a color picker; and General > UI, holding a "Show close button on tabs" toggle
  (default on) controlling whether each pane's tabs render a close ("x") button, app-wide and
  applied to already-open tabs immediately. Changes in both categories apply and persist
  immediately.
* **Status bar (Architecture.md §14.27):** each pane shows a status bar at the bottom, always
  reflecting whichever tab in that pane currently has focus: the number of objects in the
  current listing (the browsed folder, or the active search/advanced-search results) and, once
  something is selected, the number of selected objects. It also hosts a quick-select field —
  typing in it live-selects every item whose name contains what's been typed so far (anywhere in
  the name, not just at the start) and scrolls to the first match, with no need to press Enter.

**File Operations:**
* Create, open, rename, delete (move to trash/permanently delete), and copy/paste files and folders.
* Double-clicking or pressing Enter/Return on a file opens it with the OS-registered default
  application; folders navigate into them the same way (Architecture.md §14.11).
* File properties inspection (size, creation date, modification date, file type).
* **Multi-selection**: rubber-band drag-select (click-drag from empty space), Ctrl+click to toggle
  an individual item in/out of the selection, and Shift+click to add/remove a contiguous range from
  the last-clicked anchor — the same conventions as File Explorer (Architecture.md §14.16).
* **Keyboard hotkeys** (act on the whole current selection): Ctrl+C copy, Ctrl+X cut, Ctrl+V paste,
  Delete moves the selection to the Recycle Bin, Shift+Delete deletes it permanently, Backspace goes
  to the parent folder. Typing a letter `[a-z]` selects the next item whose name starts with that
  letter, cycling through matches on repeated presses. In Details view only, ArrowLeft also goes to
  the parent folder and ArrowRight opens a selected folder (no effect on a selected file). Ctrl+C/
  X/V read and write the real OS clipboard, so cut/copy/paste interoperate with File Explorer and
  other applications, not just within Exp-LORer.
* **Right-click context menu** on a file, a folder, or empty space within a directory view, drawn
  as the app's own custom menu rather than the OS's native popup (Architecture.md §14.13). Content
  is still sourced from the Windows Registry, so it varies by file type and differs between a file,
  a folder, and empty space (which shows folder-level actions like Paste/New/Refresh instead) the
  same way Explorer's does; a user-toggleable "extended" mode additionally pulls in installed
  third-party shell extensions (7-Zip, Git, etc.) via the same COM mechanism Explorer uses, at the
  cost of extra latency per right-click — the default mode reads registry verbs only. Cut/Copy/
  Paste/Delete/Rename/New Folder/Properties are always the app's own native actions, not
  registry-sourced. Cut/Copy/Delete act on the whole current multi-selection, matching the keyboard
  hotkeys above; Rename, Properties, and Open remain single-item actions and are disabled whenever
  more than one item is selected (Architecture.md §14.16).

**Search and Filter:**
* Fast filename and extension search within the current directory and subdirectories
  (Architecture.md §14.18). A search box in the toolbar, next to the split-layout controls, searches
  recursively under whichever pane/tab currently has focus once Enter is pressed; results replace
  that tab's folder view in place (other tabs/panes are unaffected). While that tab is showing
  results, further typing in the same box live-filters them without needing Enter again. Switching to
  a different tab never triggers a search for it — a tab's search results and query persist,
  independent of which tab or pane currently has focus.
* **Advanced search pane** (Architecture.md §14.19), a second and separate entry point from the
  quick search box above, opened via an "Advanced Search..." toolbar action: a dedicated per-tab
  pane with Name, Min size, Max size, and Extension fields plus a Search button, searching
  recursively under the focused tab once Search is pressed (or Enter in the Name field). Editing
  any field afterward live-refines the already-fetched results without re-scanning disk. Results
  render in a dedicated view — always styled like Details view but with taller rows and the
  matching part of each Name highlighted — regardless of the tab's own chosen view mode. Only one
  of normal browsing, quick search, or advanced search is shown at a time per tab; opening either
  search mode closes the other, and each remembers its own fields/results while hidden.
  **Tag criteria** (Architecture.md §14.22) act as a fifth, ANDed criterion alongside Name/Min
  size/Max size/Extension: a "search tags" box lists matching tags (same lookup as the Tag
  Panel's search, but clicking a tag adds it straight to the criteria instead of showing a "+"),
  and a separate, removable chip list shows the tags currently applied to the search. A result
  must satisfy both the file-property criteria and every applied tag to appear.

### 2.2 Database-Backed File Tagging System

**Tag Management:**
* Users can create, edit, delete, and color-code custom tags (e.g., "Work", "Urgent", "ProjectA", "Favorite").
* A **Tag Manager dialog** (Edit menu → "Tag Edit...", Architecture.md §14.17), separate from the tag
  panel below, manages the global tag list independent of any specific file/folder selection: a
  search bar filters the full tag list live as the user types, and Rename/Add/Delete buttons act on
  the selected tag (Add creates a new tag by name; Delete removes a tag and all of its file/folder
  associations, after confirmation).

**Tag Assignment:**
* Ability to attach multiple tags to any file or folder.
* Context-menu integration and drag-and-drop tagging.

**Database Backend:**
* Local lightweight database (e.g., SQLite) to persist tag metadata, associating file paths (or unique file hashes) with tag IDs.


**Tag-Based Filtering & Search:**
* Dedicated view/panel to filter files by one or multiple tags (e.g., show all files tagged with "Work" *AND* "ProjectA").
* Robust handling of file path changes (handling renames/moves gracefully by tracking file hashes or updating paths in the database).

**Tag Panel:**
* Housed in a collapsible, tabbed side panel (Architecture.md §14.24) on the right edge of the
  window — collapsed by default, expanding when its "Tags" tab is clicked, with room for
  additional tabs alongside it later. The panel's own tag behavior below is unchanged by this.
* A panel sharing the window with the workspace pane area shows, for whichever pane/tab currently
  has focus: (1) the tags of the active pane's current folder together with every ancestor
  folder's tags (ancestors listed before the folder's own tags); (2) a search box over all tags;
  (3) the tags matching that search, live-updating as the user types, each addable via a "+" to
  the currently selected file/folder in the active pane; (4) the tags of only that selected item
  (not its ancestors), each removable via an "x". Folder tags are not inherited by files/folders
  nested inside them — they only ever apply to the folder they were assigned to.
* When no item is selected in the active pane, the panel's add/remove sections (3 and 4) target
  the currently browsed folder itself, so a folder can be tagged without first navigating into it.
* **Tags are clickable** everywhere they appear in this panel (folder tags, matching tags, tags
  on selection, Architecture.md §14.22): clicking one opens the Advanced Search pane
  (Specification.md §2.1) on the currently active tab with that tag added as a search criterion,
  giving a one-click path from "see a tag" to "find everything else with that tag."

**Preview Panel:**
* Housed as a second tab, "Preview" (Architecture.md §14.25), alongside "Tags" in the same
  collapsible right-edge panel (Architecture.md §14.24) — collapsed by default, expanding when
  clicked.
* Shows a preview of whatever is selected in the currently focused pane/tab (or its browsed folder
  if nothing is selected): a folder's immediate contents (folders first, then files, both
  alphabetical); a text/source file's content (txt/html/xml/json/md/ini/log/yaml/csv, common
  source extensions such as cs/cpp/h/hpp/c/py/js/ts/css); a scaled preview image for supported
  image formats (2.3); or a static representative frame for supported video formats (2.4) — full
  video playback belongs to the built-in video player below, not this panel. Anything else shows
  "no preview available."
* **Syntax highlighting** (Architecture.md §14.26): recognized source files (C, C++, C#, Python,
  JavaScript, TypeScript, JSON, HTML, CSS in this pass) are shown with tree-sitter-based syntax
  highlighting rather than plain monospace text; other accepted text extensions remain plain text.
  Token colors are customizable per language (see the Settings dialog below).
* Generation happens in the background so the UI never freezes; switching the selection cancels
  whatever preview was still loading for the previous one. Recently viewed previews are cached for
  faster redisplay.

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
* Automated UI testing — manual smoke testing only for v1 (a minimal opt-in blackbox smoke check
  exists outside the v1 CTest suite, Architecture.md §11; comprehensive automated UI coverage
  remains excluded).
* Internationalization/localization — UI strings are English-only for v1.
* Integrated terminal (Architecture.md §14.23) limitations: Windows-only (`cmd.exe` via ConPTY, no
  Linux shell backend); no text selection or copy/paste inside the terminal; only one terminal
  instance per tab; no 256-color/truecolor rendering; panel open/closed state and size are not
  persisted across restarts; the terminal's working directory is fixed at first use and does not
  follow later navigation within that tab.

