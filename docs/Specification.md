
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
* **Media Viewer mode** (Architecture.md §14.61): a focused, full-pane gallery view (entered via the
  View menu) that recursively lists every image or video under the current folder as thumbnail
  tiles, filterable by AND-combined tags and a name filter, with the side/bottom panels replaced by
  a dedicated right-panel picker for the duration of the mode.
* **Folder tree** (Architecture.md §14.41): a collapsible, resizable side panel on the left edge of
  the window (collapsed by default, same behavior as the right-hand panel) with a "Tree" tab
  showing only folders, rooted at "This PC". It always follows the active pane's active tab —
  expanding down to and selecting that tab's current folder whenever the tab navigates or another
  tab becomes active. Single-clicking a folder navigates the active tab there (clicking the expand
  arrow only expands/collapses); right-clicking a folder shows the same context menu as in the file
  view; F2 renames a folder inline and Delete/Shift+Delete deletes it after confirmation. Hidden
  folders follow the "Show hidden files/folders" setting, except that hidden folders leading to the
  active tab's folder are always shown. Renaming or deleting a folder that any tab is currently
  browsing (or browsing inside) moves that tab to the renamed folder, or to the deleted folder's
  parent, rather than leaving it on a path that no longer exists.
* Standard navigation controls (Back, Forward, Up, Path Bar).
* **Path bar folder autocomplete** (Architecture.md §14.21): as the user types, a popup lists only
  the matching folders (never files) under the implied parent directory, navigable with the
  keyboard or mouse, with inline autocompletion of an unambiguous match.
* **Path bar environment variable expansion** (Architecture.md §14.54): typing a Windows-style
  `%NAME%` reference (e.g. `%appdata%`, `%USERPROFILE%\Documents`) and pressing Enter navigates to
  the expanded real path; an unset/unknown variable is left as-is, same as Explorer.
* **"This PC" virtual location** (Architecture.md §14.15): lists all local and remote drives, plus
  quick access to the Downloads, Documents, Pictures, Videos, Music, and Desktop folders. Reached
  by pressing Up (or Backspace) from any drive's root directory — the point where a regular folder
  hierarchy "exits" onto the drive listing — or by typing "This PC" into the address bar. This is
  also the default landing location for a fresh install (see below) and for any newly-revealed
  empty pane.
* **Favorites** (Architecture.md §14.36): users bookmark files and folders via a `Favorites` top-level
  menu — "Add to Favorites" (the active tab's current selection, or its browsed folder if nothing is
  selected) and "Manage Favorites..." (remove, reorder, add manually, and organize into categories
  shown as collapsible sections). The menu itself lists every favorite below "Manage Favorites...",
  in the order set in that dialog — a categorized favorite nests under a submenu named after its
  category, an uncategorized one appears directly. Clicking a favorited folder navigates the active
  tab into it; clicking a favorited file navigates to its parent folder with the file selected.
* Support for long paths (beyond the traditional 260-character Windows limit) and Unicode file/folder names throughout navigation, search, and tagging.
* **Split-window layouts:** The window arranges into one of four fixed layouts — single pane, two-pane vertical split, two-pane horizontal split, or a four-pane grid — chosen from the View menu/toolbar. No arbitrary or recursive nesting.
* **Multi-tab browsing, per pane:** Each pane independently owns its own tabs (add/close/switch), each retaining its own navigation history (back/forward stack), current path, view mode, and sort order; a pane's tabs are unaffected by other panes or by switching layouts (a pane's tabs persist even while hidden by a layout that shows fewer panes). The tag filter panel and media preview are single shared instances that follow whichever pane/tab currently has focus, rather than existing per-pane. **Session persistence:** the active layout, each pane's tabs (path, view mode, sort order), the focused pane, and the main window's geometry are saved to a local JSON config file when the app closes and restored the next time it starts (Architecture.md §14.14); a fresh install with no saved config falls back to a single pane opened at the "This PC" virtual location (Architecture.md §14.15), rather than the user's home directory.
* **Sort order:** Directory contents can be sorted by Name, Size, Type, or Date modified, ascending or descending, via a "Sort by" submenu under the View menu (Architecture.md §14.12) or by clicking a column header in Details view. The chosen sort applies uniformly across all seven view modes and is preserved per tab across navigation within that tab; a newly opened tab starts at the default (Name, ascending) rather than inheriting another tab's choice.
* **Customizable Details-view columns (Architecture.md §14.39):** right-clicking a Details-view
  column header opens a menu listing every available column — Name, Size, Type, Date Modified,
  Git Status, Tags, Date created, Percent, and Fragments — each checked when currently shown;
  clicking one toggles its visibility. This applies to the current tab only, leaving other open tabs
  unaffected. Name is always shown and isn't offered in the menu. The Tags column shows each
  file/folder's tags as small colored chips, live-updating as tags are assigned/removed; Date created
  shows the filesystem creation timestamp; Percent shows a blue-ish bar, filled left-to-right in
  proportion to the item's share of its folder's total size, with the percentage centered in the bar
  as text; Fragments shows a file's cached NTFS fragment count from the last Fragmentation-tab scan
  that covered its folder (Architecture.md §14.53) — blank if that folder was never scanned, and
  always blank for folders themselves, since fragmentation is tracked per file only; it never
  triggers a scan on its own. `Settings > General > Columns` (see below) controls which columns a
  newly opened tab starts with; it doesn't change any tab already open.
* **Hidden files/folders:** A "Show hidden files" toggle under the View menu (Architecture.md
  §14.22) controls visibility of OS-hidden entries app-wide and persists across restarts. When on,
  hidden files/folders appear in every view mode and in search results with grayed-out text so
  they're visually distinguishable; when off, they're excluded from directory listings, search
  results, and address-bar autocomplete entirely.
* **Live directory update (Architecture.md §14.33):** a folder currently browsed in a *shown* tab
  (the active tab of each pane the current split layout makes visible — one tab in Single, two in
  a split, four in the grid) automatically reflects files being created, deleted, or modified
  directly inside it, without pressing Refresh. Only that folder's immediate contents are watched
  — a change inside a subfolder does not trigger a refresh. A pane hidden by the current layout, or
  a tab that isn't its pane's active one, is not live-updated; switching to it (or revealing its
  pane) catches it up once at that point.
* **Folder monitoring (recursive, per-tab, Architecture.md §14.34):** a third right-panel tab,
  "Monitoring" (alongside "Tags" and "Preview" — see below), watches a tab's current folder and
  every subfolder beneath it for file/folder creation, deletion, and modification — meant primarily
  for watching a long-running build without needing to keep that tab in view. "Start Monitoring"
  begins recursively watching the focused tab's current directory (the button becomes "Stop
  Monitoring"); each detected change appends a row — a file/folder icon plus the changed item's path
  relative to the monitored root, colored green for an addition, red for a deletion, yellow for a
  modification — to an auto-scrolling, 200-row ring-buffer list, while an uncapped "Total" counter
  at the bottom keeps counting every change for the whole session. Navigating the monitored tab
  deeper into the watched subtree doesn't disturb monitoring; navigating it anywhere outside that
  subtree (Up, an unrelated address-bar path, another drive, etc.), closing the tab, or closing the
  application prompts for confirmation first, since any of those would silently stop the watch. A
  "pin" toggle keeps that tab's
  monitoring session shown in the panel even after the user switches focus to a different tab to do
  something else in the meantime; unpinned, the panel instead follows whichever tab currently has
  focus.
* **Git status badges (Details view, Architecture.md §14.32):** when the currently browsed folder
  is inside a git working tree, a "Git" column at the right of "Date modified" shows a small colored
  badge for any file or folder with a non-clean status — a purple "?" for untracked, blue "M" for a
  staged modification, yellow "M" for an unstaged one, green "+" for a staged addition, red "-" for
  a staged deletion, orange "-" for an unstaged deletion, grey "I" for ignored — computed in the
  background so browsing never blocks on it. Each item shows at most one status. A folder's badge
  reflects the most important change anywhere in its subtree (not just the folder itself), so a
  modified file nested inside a subfolder is still visible without opening every intermediate
  folder — an ignored file never outranks a real change (modified/added/deleted/untracked/
  conflicted) elsewhere in the same subfolder. Badges appear only in Details view, only while
  normally browsing (not in quick-search/advanced-search results), and refresh only on navigation,
  not live.
* **Git branch indicator and checkout (per-pane status bar, Architecture.md §14.35):** when the
  focused tab's current folder is inside a git working tree, the pane's status bar (see "Status bar"
  below) shows the current branch name — or a short commit id when `HEAD` is detached — centered in
  the bar, immediately followed by a plain number of changed files in the repository. Clicking the
  branch name opens a menu listing the repository's local branches; choosing one checks that branch
  out. A checkout that would overwrite uncommitted changes is refused (with an error message) rather
  than discarding anything, the same safety plain `git checkout` gives by default. Both the branch
  name and the changed-file count are hidden when the current folder isn't inside a git repository,
  and refresh on navigation, not live.
* **Integrated terminal (Architecture.md §14.24):** Each tab has its own collapsible panel at the
  bottom, collapsed by default, showing a strip of panel tabs ("Terminal", "Folder Size", and
  "Fragmentation" in v1). Clicking "Terminal" the first time expands the panel and opens a default
  `cmd.exe` session already in that tab's current folder. A right-side list inside the Terminal
  panel tab lets the user open further sessions — choosing Command Prompt or PowerShell from a '+'
  dropdown — switch between them, and close any one independently; collapsing and re-expanding the
  panel leaves all still-running sessions in place. Closing the tab ends every one of its terminal
  sessions.
* **Disk usage / folder size (Architecture.md §14.40):** the bottom panel's "Folder Size" tab shows a
  pie chart of the current folder's largest subfolders/files (up to 10 segments, each labeled with
  name, percentage of the folder's total size, and size, color-coded, and clickable to navigate into
  that subfolder) alongside "Scan Drive" and "Scan Folder" buttons. Scanning runs in the background
  (button text changes to "Scanning..." while in progress, and a cancellable progress notification
  shows a live running count and the path currently being examined — Architecture.md §14.37) and,
  once complete, updates the chart and saves the scanned folder sizes to the local database for reuse
  without rescanning. Scanning itself
  is a self-contained tool, not integrated with tagging, search, or the Preview panel — but once a
  folder has been scanned, its cached recursive size also appears in the Details view's Size column
  for that folder (wherever it's listed as a subfolder), instead of staying blank; a folder never
  scanned keeps showing a blank Size cell, and this never triggers a scan on its own. The Details
  view's Percent column (see above) uses the same per-folder cached sizes: each row's bar is that
  item's size divided by the sum of all its siblings' sizes in the current folder, so a subfolder
  that has never been scanned has no size to contribute and shows no bar of its own until scanned.
* **Disk fragmentation viewer (Architecture.md §14.52):** a third bottom-panel tab, "Fragmentation",
  next to "Folder Size" — scans the tab's current folder or its drive and reports NTFS file
  fragmentation; viewing only, no defragmentation action. A summary (files scanned, total size
  scanned, how many files are fragmented, total fragment count) sits above a sortable per-file list
  (name, folder, size, fragment count, and a contiguous/fragmented state), filterable to fragmented
  files only. Unlike the duplicate file finder below, scan results persist to their own local
  database table — navigating away and back (or restarting the app) redisplays the last scan for a
  folder instantly, without rescanning; rescanning refreshes it. Once a folder has been scanned, its
  files' cached fragment counts also appear in the Details view's Fragments column (see above),
  the same cache-only posture as Folder Size's Size-column integration described above it.
* **Settings dialog (Architecture.md §14.27):** `File > Settings...` opens a dialog with a
  hierarchical category tree on the left and that category's settings on the right; in v1 this
  holds five categories: Highlight > Languages, listing the languages with syntax highlighting
  (2.2 "Preview Panel") and, per language, each of its token types with a color swatch the user can
  change via a color picker; General > UI, holding a "Show close button on tabs" toggle
  (default on) controlling whether each pane's tabs render a close ("x") button, app-wide and
  applied to already-open tabs immediately; General > Colors (Architecture.md §14.30), an
  editable, ordered list of file/folder appearance rules (see below); General > Notifications
  (Architecture.md §14.37), a fixed 3-row table (Type / Enabled / Timeout) letting the user
  enable/disable each popup notification type and set how long it stays on screen before
  auto-dismissing; and General > Columns (Architecture.md §14.39), a "Default visibility for new
  tab" group box with one checkbox per available Details-view column (Name excluded — always
  visible) controlling which columns a newly opened tab starts with. Changes in all five
  categories apply and persist immediately, though Columns' effect is scoped to tabs opened from
  then on, not already-open ones.
* **Notifications (Architecture.md §14.37):** two kinds of app-wide notification appear stacked in
  the bottom-right corner of the window. Popup notifications — Info (blue), Warning (yellow), or
  Error (red), each showing an icon/type header and a message body — auto-dismiss after a
  configurable timeout (see Settings above) and always have a manual close button. Progress-bar
  notifications can show one or more progress bars for a long-running operation, each with a label
  above and a smaller-font label below it, and an optional Cancel button; copy and move operations
  that need to transfer real bytes drive one of these (see "File Operations" below,
  Architecture.md §14.47).
* **Per-file/folder appearance customization (Architecture.md §14.30):** under Settings >
  General > Colors, the user maintains an ordered list of rules, each pairing a comma-separated set
  of name patterns with a style (custom color, custom font, and independently toggleable bold,
  italic, underline, and strikeout). Patterns support `*` (any characters), `?` (a single
  character), `"..."` to escape a literal comma inside a pattern, and a leading `#` to mark a
  pattern as matching folders instead of the default of files only — e.g. `*.exe` (files with the
  exe extension), `Image_?.png` (matches `Image_1.png`/`Image_2.png` but not `Image_2121.png`), or
  `#.git` (a folder literally named `.git`). For any given file or folder, the first rule in the
  list with a matching pattern determines its appearance everywhere it's listed (browsing, quick
  search, and advanced search, in every view mode); rules can be reordered, since order determines
  precedence when more than one would match.
* **Status bar (Architecture.md §14.28):** each pane shows a status bar at the bottom, always
  reflecting whichever tab in that pane currently has focus: the number of objects in the
  current listing (the browsed folder, or the active search/advanced-search results) and, once
  something is selected, the number of selected objects. It also hosts a quick-select field —
  typing in it live-selects every item whose name contains what's been typed so far (anywhere in
  the name, not just at the start) and scrolls to the first match, with no need to press Enter.

**File Operations:**
* Create, open, rename, delete (move to trash/permanently delete), and copy/paste files and folders.
* **Copy/move progress (Architecture.md §14.47):** pasting a cut/copied selection that stays on the
  same drive for a move is instant, exactly as before. Every other case (any copy; a move across
  drives) shows a progress notification: first an "Estimating" phase (file count and total size,
  updated about once a second), then — if the destination doesn't have enough free space — a
  dialog and the operation stops before anything is copied, otherwise a "Copying"/"Moving" phase
  (current file's relative path and size, overall bytes transferred) with a Cancel button that
  stops the transfer as soon as the file in progress can be safely interrupted. A short
  notification confirms completion; cancelling shows no such notification, and files already
  transferred before a cancel are not undone.
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
* **Duplicate file finder (Architecture.md §14.51):** `Tools > Find Duplicate Files...` opens a
  dialog that recursively scans a chosen folder or drive for files with byte-identical content —
  grouped by size, then a full-content hash, then a byte-for-byte confirmation pass, deliberately
  not the fast heuristic identity hash used elsewhere (§8) since that one mixes in modification time
  and would misclassify both identical and genuinely different files. Results list each duplicate
  group with per-file checkboxes ("keep newest/oldest" helpers included) and a "Delete Checked"
  action that sends the chosen copies to the Recycle Bin. No duplicate index is cached — every scan
  reads the live filesystem, since pairing a stale result with a delete action would be a
  correctness risk.
* **Bulk/batch rename (Architecture.md §14.55):** rename several files/folders at once — find/
  replace (plain or regex, with optional case-sensitivity), a sequential counter (start value, step,
  zero-padded digits, prefix or suffix position), a case transform (UPPERCASE/lowercase/Title Case),
  and an extension override, combined into one pattern and applied to every selected item. A live
  preview table shows each item's current and proposed name before anything is renamed, flagging any
  proposed name that's unchanged, invalid, or collides with another item (in the same batch or
  already on disk); renaming is disabled until every flagged row is resolved. Reached either from the
  item context menu's "Bulk Rename..." entry (enabled once 2+ items are selected — single-item
  rename stays F2/"Rename") or from `Tools > Bulk Rename...`, which uses the current selection if 2+
  items are selected, otherwise the whole current folder's listing.
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
* **Search tab** (Architecture.md §14.42), on the collapsible left panel alongside the "Tree" tab
  (§14.41) — replaces the former toolbar search box as the fast filename/extension search entry
  point. A small icon toolbar (Refresh — re-runs the current search; Clear — empties both the
  search bar and the results; View as tree — renders results grouped by containing folder instead
  of a flat list; Collapse all — collapses the tree view, enabled only while View as tree is on)
  sits above a search bar. The search bar searches recursively under whichever pane/tab currently
  has focus, live as the user types — no Enter press required — performed asynchronously so the UI
  is never blocked, even on a large folder; search text highlighting updates instantly, while the
  search itself settles ~500ms after the last keystroke (Architecture.md §14.46) rather than
  re-running on every single one, so rapid typing in a large folder stays responsive; an empty
  search bar performs no search and shows no results. It supports `*` (any run of characters) and
  `?` (exactly one character) wildcards — the
  same syntax as the per-file/folder appearance patterns elsewhere — e.g. `*.pdf` (all PDFs),
  `doc*` (`doc`, `document.txt`), or `file?.txt` (`file2.txt` but not `file12.txt`); a query with
  no wildcard still matches anywhere in the name, as before. Results show only each match's name,
  with the part satisfying the query highlighted, as either a flat list (default) or, with View as
  tree toggled, a tree grouped by the folders between the search root and each match. Double-
  clicking a result opens it in the active tab: a file opens its parent folder with the file
  itself highlighted; a folder opens directly. Switching which pane/tab is focused retargets the
  Search tab to that tab's own search state (query, results) — each tab's search persists
  independently while another tab is focused, same as before. Above the search bar, four toggle
  buttons (Architecture.md §14.44) narrow how the query text is interpreted: **Match whole word**,
  **Exact name**, and **Match regex** are mutually exclusive with each other and with the default
  substring/wildcard behavior (selecting one deselects the others; clicking the active one again
  returns to the default) — Whole word requires the query to appear as a separate word (e.g. `File`
  matches `File Name` but not `FileName`), Exact name requires the query to equal the entire name,
  and Match regex treats the query as a regular expression. **Match case** is independent and
  combines with any of the above, including the default (off by default, so `File` and `file` match
  each other until it's enabled). Toggle state is per-tab and not persisted across restarts, same as
  View as tree. The Advanced Search pane (below) has its own copy of these same 5 toggles, applied
  independently to its own Name field.
* **Advanced search pane** (Architecture.md §14.19/§14.56) — a dedicated per-tab pane with Name,
  Min size, and Max size fields plus a Search button, opened by an "Advanced Search..." button now
  living beside the Search tab's search bar (Architecture.md §14.42/§14.46) instead of on the main
  toolbar. The Name field supports `*`/`?` wildcards, same as above; there is no separate Extension
  field — a `*.ext`-style Name query covers that case. Above the fields, the same 5 toggle buttons
  as the Search tab (Match whole word/Exact name/Match regex mutually exclusive with each other and
  the default; Match case independent; Skip git-ignored files/folders independent) narrow how the
  Name field is interpreted; toggling Skip git-ignored re-scans, since it changes what gets scanned
  rather than just how results are filtered. Opening the pane, or clearing every field (Name, Min
  size, Max size, and every tag criterion) back to empty while the pane is already open, shows the
  focused tab's current folder contents (a plain, non-recursive listing, not a search) — so it's
  never left showing a stale result set or, worse, every file in the subtree; searching recursively
  only happens once at least one criterion is set and Search is pressed (or Enter in the Name
  field, or a field is edited live). Editing any field afterward live-refines the already-fetched
  results without re-scanning disk, except for the Skip git-ignored toggle noted above. Results
  render in a dedicated view, in place of the tab's own folder view — always styled like Details
  view but with taller rows and the matching part of each Name highlighted — regardless of the
  tab's own chosen view mode.
  **Tag criteria** (Architecture.md §14.20) act as a fourth, ANDed criterion alongside Name/Min
  size/Max size: a "search tags" box lists matching tags (same lookup as the Tag Panel's search,
  but clicking a tag adds it straight to the criteria instead of showing a "+"), and a separate,
  removable chip list shows the tags currently applied to the search. A result must satisfy both
  the file-property criteria and every applied tag to appear. An **Exact/Include-Children toggle**
  (Architecture.md §14.87) controls how each applied tag is matched against nested tags (§2.2):
  exact (default) matches only that literal tag, while include-children also matches any tag
  nested under it.

### 2.2 Database-Backed File Tagging System

**Tag Management:**
* Users can create, edit, delete, and color-code custom tags (e.g., "Work", "Urgent", "ProjectA", "Favorite").
* A **Tag Manager dialog** (Edit menu → "Tag Edit...", Architecture.md §14.17/§14.89), separate from
  the tag panel below, manages the global tag list independent of any specific file/folder selection: a
  search bar filters the full tag list live as the user types, and Rename/Add/Delete buttons act on
  the selected tag (Add creates a new tag by name; Delete removes a tag and all of its file/folder
  associations, after confirmation). A toggle shows each tag's file count on its chip, and selecting a
  tag lists every file/folder tagged with it below the chip row (mirroring the Advanced Search results
  list). Renaming a tag to a name that already exists prompts to merge the two tags (moving every
  file/folder association onto the existing tag and removing the renamed one) instead of failing with
  an error.

**Tag Assignment:**
* Ability to attach multiple tags to any file or folder.
* Context-menu integration and drag-and-drop tagging.
* Applying or removing a tag shows an Info popup notification confirming the change
  (Architecture.md §14.37).

**Database Backend:**
* Local lightweight database (e.g., SQLite) to persist tag metadata, associating file paths (or unique file hashes) with tag IDs.


**Tag-Based Filtering & Search:**
* Dedicated view/panel to filter files by one or multiple tags (e.g., show all files tagged with "Work" *AND* "ProjectA").
* Robust handling of file path changes (handling renames/moves gracefully by tracking file hashes or updating paths in the database).
* **Nested tags**: a tag name may be split into levels with `/` (e.g. `"game/action"`), making
  `"game"` the parent of `"game/action"`. This is a naming convention only — no separate parent
  tag needs to exist, and nothing elsewhere in the tagging UI changes because of it (Architecture.md
  §14.87). Advanced Search's tag criteria (§2.1) can optionally match a tag's nested children too.

**Tag Panel:**
* Housed in a collapsible, tabbed side panel (Architecture.md §14.25) on the right edge of the
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
  on selection, Architecture.md §14.20): clicking one opens the Advanced Search pane
  (Specification.md §2.1) on the currently active tab with that tag added as a search criterion,
  giving a one-click path from "see a tag" to "find everything else with that tag."

**Preview Panel:**
* Housed as a second tab, "Preview" (Architecture.md §14.26), alongside "Tags" in the same
  collapsible right-edge panel (Architecture.md §14.25) — collapsed by default, expanding when
  clicked.
* Shows a preview of whatever is selected in the currently focused pane/tab (or its browsed folder
  if nothing is selected): a folder's immediate contents (folders first, then files, both
  alphabetical); a text/source file's content (txt/html/xml/json/md/ini/log/yaml/csv, common
  source extensions such as cs/cpp/h/hpp/c/py/js/ts/css); a scaled preview image for supported
  image formats (2.3); or, for supported video formats (2.4), a static representative frame by
  default with a dropdown to switch to the inline built-in video player instead (Architecture.md
  §14.67). Anything else shows "no preview available."
* **Syntax highlighting** (Architecture.md §14.27): recognized source files (C, C++, C#, Python,
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
* Play, pause, seek bar (scrubbing), volume control, and mute — implemented (Architecture.md
  §14.67) as an inline Qt Widgets/Qt Multimedia player (`VideoPlayerWidget`), reachable from the
  Preview panel's mode dropdown (2.2) and reusable elsewhere later.
* Fullscreen toggle and aspect ratio adjustments — not yet implemented.
* Loop playback option — not yet implemented.


**Previewing:**
* Thumbnail generation for video files in the grid/list view.



### 2.5 Archive Browsing, Preview, and Extraction (Read-Only)

* Common archive files (ZIP, 7-Zip, TAR, TAR.GZ, TAR.BZ2, and RAR — RAR read-only) can be browsed
  as if they were a folder: double-clicking/pressing Enter on one navigates into it in the same
  window rather than launching a separate archive-manager application, with full support for the
  standard navigation controls (Back/Forward/Up/breadcrumb), sorting, view modes, and multi-
  selection while inside it (Architecture.md §14.29).
* Selecting an archive file (without entering it) shows its top-level contents in the Preview
  panel (2.2), the same way a folder's contents are shown.
* Right-clicking an archive file offers archive-specific actions ("Extract Here", "Extract to...")
  in addition to the standard file context menu; while browsing inside an archive, the context
  menu drops actions that don't apply to a read-only, virtual location (Cut, Paste, Rename,
  Delete, New Folder, Properties) and offers an equivalent Extract action for the selected
  entry/entries instead.
* **V1 is read-only**: creating a new archive, or adding/removing/modifying files inside an
  existing one, is out of scope (see §5).

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
* **Single Instance (per workspace):** Running two instances of the application against the *same* workspace simultaneously is not supported/allowed — launching a second one must detect the existing instance for that workspace and activate its window instead of starting a new process, to avoid concurrent writers to the same workspace's session state. This is deliberately scoped per `(app-data directory, workspace)`, not per app-data directory alone (Architecture.md §9/§6/§14.60): different workspaces — see Workspaces, below — are meant to run as independent, fully concurrent processes, each its own window; instances started against different `--data-dir` overrides (e.g. parallel UI test runs) are likewise independent of each other and of the real session.
* **Workspaces:** A workspace is a named snapshot of the window session (active layout, each pane's tabs/paths/view mode/sort order, the focused pane, panel state, window geometry — the same state Session Persistence above already saves/restores). File > Workspaces lists every saved workspace (a fresh install has exactly one, "Default") and opens the chosen one in its own process/window; File > Save as Workspace... names and persists the current window's configuration as one; File > Workspace Management... lets the user rename or delete any saved workspace. Deleting or renaming a workspace that's currently open in another window is handled without corrupting either window's state (Architecture.md §14.60).
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
* Archive creation and in-archive editing (2.5): browsing/preview/extraction only, no
  "compress to archive" / "add to archive" support, and no nested-archive browsing.
* Automated UI testing — manual smoke testing only for v1 (a minimal opt-in blackbox smoke check
  exists outside the v1 CTest suite, Architecture.md §11; comprehensive automated UI coverage
  remains excluded).
* Internationalization/localization — UI strings are English-only for v1.
* Integrated terminal (Architecture.md §14.24) limitations: Windows-only (`cmd.exe` and
  `powershell.exe` via ConPTY, no Linux shell backend, no other shells/arbitrary commands); keyboard-
  only selection (character/word via Shift/Ctrl+Shift+Left/Right, select-all via Ctrl+A) and
  clipboard (Ctrl+Shift+C/V) — no mouse drag-to-select; no 256-color/truecolor rendering; the open-
  sessions list, active session, chosen shells, and panel open/closed state/size are not persisted
  across restarts; each session's working directory is fixed at creation and does not follow later
  navigation within that tab.

