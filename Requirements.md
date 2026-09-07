
# Requirements Document

## 1. Project Overview

### 1.1 Purpose

The purpose of this project is to develop a custom desktop file explorer in C++ that goes beyond traditional file management. It integrates a database-backed tagging system for advanced file organization and includes a built-in media viewer (images and videos) to provide a seamless browsing and preview experience without needing external applications.

### 1.2 Target Platform

* **Operating System:** Cross-platform (Primary focus on Windows/Linux).
* **Language:** C++ (C++17 or C++20).
* **UI Framework:** Qt (Recommended for cross-platform UI, file system models, and built-in multimedia modules) or a combination of native APIs and custom rendering.


## 2. Functional Requirements

### 2.1 Standard File Explorer Functionalities

**Directory Navigation:**
* Tree view and list/grid view of the local file system.
* Standard navigation controls (Back, Forward, Up, Path Bar).
* Ability to bookmark or pin frequently used folders.

**File Operations:**
* Create, open, rename, delete (move to trash/permanently delete), and copy/paste files and folders.
* File properties inspection (size, creation date, modification date, file type).

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

### 3.2 Reliability and Data Integrity

* **Database Consistency:** The SQLite database must handle concurrent reads/writes safely. Transactions should be used when bulk-tagging files.
* **Error Handling:** Graceful handling of permission errors, missing files, or corrupted media formats without crashing the application.


## 4. Proposed Technology Stack (Recommendations)

* **Core Language:** C++17/20
* **UI & Framework:** Qt 6
* **Database:** **SQLite** (embedded, lightweight, zero-configuration file database).
* **Media Processing (Alternative/Addition):** **FFmpeg libraries** (libavcodec, libavformat) if deeper control over video decoding and custom rendering is required.

