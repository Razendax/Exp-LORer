# Architecture Document — Feature Decision Log: File Operations & Navigation

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.10–14.13, §14.15–14.16, §14.21–14.22, §14.47, §14.54 — clipboard file operations,
opening files, the custom context menu, "This PC", multi-selection, address bar autocomplete and
environment-variable expansion, hidden files, and custom copy/move with progress notifications.
Section numbers are **not** contiguous with this file's neighbors — they were assigned
chronologically as each feature was built and are kept stable since code comments cite them
directly. See [Architecture.md](../Architecture.md) §14 for the full topic index across all nine
files.

---
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

### 14.21 Address Bar Folder Autocomplete

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

### 14.22 Hidden Files/Folders Visibility

A global "Show hidden files" toggle in the View menu controls whether OS-hidden entries appear at
all, and if so, whether they're visually distinguished. `FileNode` gains `isHidden()`, populated by
`StandardFileSystemRepository::buildFileNode()` via `GetFileAttributesW`/`FILE_ATTRIBUTE_HIDDEN` on
Windows (a leading-`.` filename convention on a future Linux backend); every existing
`FileNode`-producing repository call funnels through that one function, so no other adapter code
changes. Synthetic nodes ("This PC", drives, known folders) are never marked hidden.

`FileListModel` is the single filter/style point rather than the Application layer or
`TabViewModel`: it already backs normal browsing, §14.18 quick search, and §14.19 advanced search
alike (one instance each, all fed via `setEntries()`), so implementing it once there covers all
three. It keeps a `m_visibleRows` index over its full `m_entries`, rebuilt whenever entries/sort/the
toggle change; with the toggle on, every row stays visible; with it off, hidden rows are dropped
from the index entirely. Text/font styling of hidden rows is not hardcoded here — it's one of
§14.30's decoration mechanisms: the built-in "Hidden Files"/"Hidden Folders" rows, user-configurable
from `Settings → General → Colors` like any other decoration rule, with no styling applied by
default until the user sets one. Qt's stock `QStyledItemDelegate` (List/Details views) honors
`Qt::ForegroundRole`/`Qt::FontRole` automatically; the three custom delegates that hand-paint text
(`FileIconDelegate`, `FileTileDelegate`, `SearchResultDelegate`) each read the same roles for their
non-selected pen color/font instead of the hardcoded `option.palette.text()` they used before, so
all seven view modes plus the advanced-search results view render consistently.

The **icon** stays hardcoded-dimmed regardless of decoration rules, unlike text/font: `data()`'s
`Qt::DecorationRole` branch runs a hidden entry's icon through a `dimmedIcon()` helper that rebuilds
it one size at a time via `QStyle::generatedIconPixmap(QIcon::Disabled, ...)` — the same graying Qt
applies to a disabled toolbar action, so it already tracks the light/dark palette without needing a
color to be configured. This is deliberate: §14.30 only ever exposed a hex color/font, never an icon
override, and Explorer-style dimmed icons are expected even when a user hasn't set (or has cleared)
the Hidden Files/Hidden Folders text style, unlike the text color which intentionally starts at
"no override" (see §14.30).

The toggle itself is a `QSettings`-backed app-wide preference (`MainWindow`, modeled on
`createContextMenuModeAction()`) rather than `config.json`/`WorkspaceConfig` state — same posture as
§14.13's shell-extensions toggle, since it's one app-wide display preference, not per-tab/session
state. Toggling it persists the value and live-broadcasts to every open tab's three `FileListModel`s
(main/quick-search/advanced-search) across all four panes via `WorkspaceController::pane(id)`/
`tabAt(i)`, so already-open folders and result sets update immediately with no re-navigation or
rescan; a `FileListModel` constructed afterward (a new tab) picks up the persisted value itself.
§14.21's `TabViewModel::suggestFolders` also excludes hidden folders from autocomplete when the
toggle is off, reading the same setting on demand (autocomplete is already re-queried per keystroke,
so no live-broadcast plumbing is needed there). Not built: a per-item way to toggle a file's own OS
hidden attribute (Properties-dialog territory); any Linux-specific hidden-file semantics beyond the
one-line dotfile fallback above. Untested per §11 beyond the `FileNode`/`StandardFileSystemRepository`
attribute-detection cases (the rest is UI/ViewModel glue, same posture as §14.18/§14.19/§14.21).

### 14.47 Custom Copy/Move with Progress Notifications

Wires `pasteInto` (Ctrl+V / context-menu Paste, the only real cross-directory copy/move entry
point, §14.10) into the notification system's progress-bar mechanism (§14.37), replacing the
synchronous, no-feedback `FileNavigationUseCase::moveFile`/`copyFile` loop that was §5's long-
accepted gap — with genuine background execution, a free-space check, and cooperative
mid-transfer cancellation.

* **Fast path unchanged.** A move is first attempted as a direct `IFileSystemRepository::move`
  (`fs::rename`) on each top-level clipboard item, exactly as before. Same-volume moves succeed
  here and never touch the new pipeline at all — no progress popup for an operation that's already
  instant. Only items where that fails (cross-volume moves) and every copy (which always has to
  duplicate bytes) go through the pipeline below, batched into one progress notification per
  `pasteInto` call if the batch is non-empty.
* **`FileTransferUseCase`** (`src/application`, new, depends only on `IFileSystemRepository`, same
  "never call another use case" posture as `FilePreviewUseCase`): `planTransfer` recursively
  enumerates every source (reusing `IFileSystemRepository::listDirectoryRecursive`'s existing
  ~1/sec-throttled progress callback, accumulated into a grand total across multiple top-level
  sources) into a flat `FileTransferPlan` of files and directories with their mapped destination
  paths, file count, and total bytes; `checkDestinationHasSpace` wraps the existing
  `driveSpaceInfo` Port call; `executeTransfer` creates destination directories on demand (each
  file's parent chain is created tolerant of `AlreadyExists` immediately before that file is
  copied, so plan ordering never matters) and copies each file via the new
  `IFileSystemRepository::copyFileWithProgress` Port method, deleting the matching top-level
  source afterward only for a move whose every file finished copying. Cancellation is "best effort
  so far": already-copied files (and, for a move, their already-deleted sources) are not rolled
  back when Cancel interrupts a later file — this is a transfer that stops partway, not an
  all-or-nothing transaction.
* **`IFileSystemRepository::copyFileWithProgress`** (new Port method, alongside the existing
  one-shot `copy()`): byte-copies one real file with a periodic progress callback that doubles as
  the cooperative-cancel hook — returning `false` aborts as soon as possible and removes the
  partial destination file. Windows: `CopyFileExW` with an `LPPROGRESS_ROUTINE` trampoline
  returning `PROGRESS_CANCEL`/`PROGRESS_CONTINUE` (Windows deletes the partial file on cancel
  itself); portable fallback (Linux-later): a buffered `ifstream`/`ofstream` loop checking the
  callback between chunks and removing the partial file itself on cancel.
* **`FileTransferService`/`FileTransferWorker`** (`adapters/viewmodels`, new): one dedicated
  background `QThread`, the same owned-worker shape as `AdvancedSearchService`/`AdvancedSearchWorker`
  (§14.38) — one transfer at a time, a second concurrent paste queues behind it (accepted
  limitation, not a thread pool). Unlike Advanced Search's "cancel = discard a stale result,"
  cancellation here is a real `std::atomic<bool>` flag, created per request and flipped directly
  from the UI thread by `FileTransferService::cancel(requestId)` with no thread hop needed; the
  worker thread's copy loop observes it between chunks via `copyFileWithProgress`'s callback.
* **`NotificationCenter` gains a cancel path and two extra progress labels.**
  `createProgressNotification(title, barCount, cancellable = false)` and
  `updateProgressBar(id, barIndex, current, max, aboveText, progressText, belowText)` —
  existing/future non-cancellable, single-label callers are unaffected (`cancellable` defaults
  false, `progressText`/`belowText` can stay empty). A new `progressCancelRequested(id)` signal
  fires when the user clicks a cancellable notification's Cancel button; `FileOperationsController`
  is the only subscriber today, mapping the id (which it deliberately reuses as
  `FileTransferService`'s own request id — the two systems share one correlation key rather than
  maintaining two maps) to `FileTransferService::cancel`.
* **`ProgressNotificationWidget`** gains two more smaller-font labels below each bar (request: "Top
  label says... Copying n files with size x. Middle label shows how many files/how much size is
  copied so far. Bottom label displays relative path of file with size") and an optional Cancel
  `QPushButton` (disabled, not removed, immediately on click, so the
  notification's own `terminateProgressNotification` — arriving once the worker thread confirms
  the cancellation — is still what actually removes the card).
* **The pipeline's three stages**, reusing one progress notification end to end: *Estimating*
  (indeterminate bar, above-label "Estimating: files N, size X" updated ~1/sec from
  `planTransfer`'s progress callback) → *free-space check* (silent pass-through, or terminates the
  notification and raises a blocking `QMessageBox` via `FileOperationsController`'s new
  `insufficientDiskSpace(message)` signal, wired in `MainWindow.cpp` next to the existing
  `operationFailed` connection — no file is copied if this fails) → *Copying* (determinate bar,
  bytes copied over total bytes, above-label "Copying/Moving N files (X)" set once, below-label the
  current file's relative path + size, updated per file). On success, a single Info popup
  ("Copy/Move Complete", naming the source/destination) summarizes the whole batch; on
  cancellation, no popup (the user asked for this); on any other failure, the existing
  `operationFailed` status-bar channel is used, same as every other `FileOperationsController` slot.
* **Not built:** all-or-nothing rollback on cancel (see above); a cancellable *estimating* phase
  mid-single-huge-subtree-walk (`listDirectoryRecursive` itself has no cancel hook — a cancel
  request during estimating only takes effect at the next top-level source boundary; it always
  takes full effect once the copy phase starts); more than one concurrent transfer; drag-and-drop
  using this mechanism (drag-and-drop itself doesn't exist yet); a "Copy to.../Move to..." picker
  dialog. No CTest coverage for `FileTransferService`/`FileTransferWorker`/the
  `FileOperationsController`/`NotificationCenter`/widget changes, per §11's existing UI/ViewModel-
  glue carve-out; `FileTransferUseCase` (mocked `IFileSystemRepository`, `tests/application`) and
  `StandardFileSystemRepository::copyFileWithProgress` (real `:memory:`-adjacent temp-directory I/O,
  `tests/adapters`) get ordinary GTest coverage.

### 14.54 Address Bar Environment Variable Expansion

Typing a Windows-style `%NAME%` reference into a pane's address bar (e.g. `%appdata%`,
`%USERPROFILE%\Documents`, `C:\Users\%USERNAME%\Desktop`) and pressing Enter navigates to the
expanded real path. Expansion happens only at commit time, inside `WorkspacePaneWidget::
onAddressBarEdited()` — right after trimming and before the existing `"this pc"` comparison and
`newPath` construction — never live while typing; §14.21's folder-autocomplete popup is unaffected.
This mirrors Explorer's own address bar, which likewise only resolves `%...%` on navigate, not
while typing.

`IFileSystemRepository` gains one new Port method, `expandEnvironmentVariables(const
std::filesystem::path& input) const`, implemented by `StandardFileSystemRepository` as a direct
call to Win32 `ExpandEnvironmentStringsW` (the standard two-call size-then-fill pattern) over
`input.wstring()` — never `.string()`/`.generic_string()`, per `PathUtf8.h`'s rule, since the
expanded value itself may contain non-ASCII (e.g. a non-English Windows username under
`%USERPROFILE%`). Reusing the OS call's own semantics, rather than a hand-rolled parser, means a
`%NAME%` for a set variable is substituted, an unset/unknown `%NAME%` (or a lone `%` not part of a
well-formed pair) is left in the output unchanged — so a folder literally named `100% Done` is
untouched, and a typo'd variable name simply fails navigation the normal way via the existing
`navigationFailed` signal, with no dedicated error message needed. Multiple variables in one string,
and a variable embedded mid-path, both work for free since the whole string is handed to the OS in
one call. `FileNavigationUseCase::expandEnvironmentVariables` is a thin passthrough (same shape as
`driveSpaceInfo`); `TabViewModel::expandEnvironmentVariables` is a pure, side-effect-free query
placed next to `suggestFolders`, called once by `WorkspacePaneWidget` per commit.

Deliberate convention deviation: the Port method returns a plain `std::filesystem::path`, not
`Result<T>` — expanding environment variables can't fail (worst case is "no change"), so wrapping it
in a failure channel would be error handling for a scenario that can't happen.

Windows-only for now (Specification.md §1.2): the Port method's non-Windows branch returns `input`
unchanged. A real Linux implementation (POSIX `$VAR`/`${VAR}`, a different convention from Windows'
`%VAR%`) is deferred along with the rest of the Linux backend. Tested with mocked-Port coverage in
`tests/application` (passthrough) and real, test-controlled environment variables (set via
`_wputenv_s`/`SetEnvironmentVariableW` within the test process — deterministic and assertable, not
the §11 "no assertable return" carve-out) in `tests/adapters/StandardFileSystemRepositoryTest.cpp`:
known variable substituted, unknown variable left literal, two variables in one string, plain text
unchanged. No new CTest coverage for the `WorkspacePaneWidget` wiring itself — UI/ViewModel glue,
same posture as §14.21/§14.22; verified by manual smoke test instead.

