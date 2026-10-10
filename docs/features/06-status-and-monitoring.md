# Architecture Document — Feature Decision Log: Status & Monitoring

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign**, aside from a couple of features below
that add their own small Domain/Application pieces (called out inline where relevant) — and is
recorded as a decision log per feature, not an implementation walkthrough; see git history and
the code's own comments (many cite these subsection numbers directly) for the detail.

Covers §14.28, §14.31–14.35 — the per-pane status bar, the window-wide status bar, per-file git
status badges (Details view), live directory updates, recursive folder monitoring, and git branch
status/checkout. Section numbers are **not** contiguous with this file's neighbors — they were
assigned chronologically as each feature was built and are kept stable since code comments cite
them directly. See [Architecture.md](../Architecture.md) §14 for the full topic index across all
nine files.

---
### 14.28 Per-Pane Status Bar

A persistent status bar shown at the bottom of every pane, reporting the currently focused
tab's object count / selection count and hosting a quick-select field — distinct from
`MainWindow`'s existing `QMainWindow::statusBar()` (a transient, window-wide 5-second
auto-hiding message bar for navigation/search failures, `MainWindow::onStatusMessage`), which is
unchanged by this feature.

* **One widget per pane, rebound to the active tab** — the same shape `WorkspacePaneWidget`
  already uses for its toolbar (address bar, back/forward/up): a single `StatusBarWidget`
  instance, not one per tab page, so the existing per-tab splitter/`QStackedWidget` structure
  (`addPageForTab`, §14.24) is untouched. `bindToolBarToTab()` — already the tab-switch rebind
  point — additionally wires the new tab's `directoryContentsChanged`, `searchResultsChanged`,
  `advancedSearchResultsChanged`, `searchModeChanged`, `advancedSearchModeChanged`, and
  `selectedEntriesChanged` signals to a `refreshStatusCounts()` handler that reads the counts
  directly off the tab (`advancedSearchActive()`/`searchActive()` pick which of
  `fileListModel()`/`searchResultsModel()`/`advancedSearchResultsModel()`'s `rowCount()` is
  "current", plus `selectedEntries().size()`), so it's correct regardless of which signal fired.
* **`StatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`: a `QLabel` (`"<N> items"`, or `"<N> items, <M> selected"` once something is
  selected) plus a `QLineEdit` quick-select field emitting `quickSelectTextChanged(QString)` on
  every keystroke; Escape clears it. No `TabViewModel`/`FileListModel` dependency — it only
  emits signals, `WorkspacePaneWidget` mediates, exactly like the address bar's
  `folderSuggestionsRequested` (§14.21). Fixed height, appended below `m_tabWidget` in
  `WorkspacePaneWidget`'s own top-level layout (not inside any per-tab splitter).
* **Quick select** (`FileBrowserView::selectEntriesContaining(text)`, new) — a linear scan over
  the currently active tab page's model (mirroring `indexForPath`'s existing scan), matching
  against the Name column's *displayed* text (`Qt::DisplayRole`, so synthetic `displayName()`
  overrides like "This PC" drives, §14.15, match what's typed) rather than `FileNode::name()`
  directly. Every case-insensitive substring match (anywhere in the name, not just a prefix) is
  selected; the first match is scrolled into view. Empty text is a no-op (leaves the existing
  selection alone); no match clears the selection. `WorkspacePaneWidget` resolves "the currently
  active tab page's browser view" via a new `currentBrowserView()` helper, factored out of the
  unwrap `onViewModeChanged()`/`currentColumnWidths()` already duplicated (splitter →
  `QStackedWidget` → `FileBrowserView`-or-`SearchResultsPane::browserView()`).
* **Reset points:** the quick-select field is cleared (`StatusBarWidget::clearQuickSelect()`) on
  tab switch and on the bound tab's `currentPathChanged`/search-mode-toggle signals, since the
  visible item set changes completely at each of those points — same ephemeral-per-context
  posture as the quick-search/advanced-search boxes' own state (§14.18/§14.19).
* **Not built:** cycling through multiple matches (Tab/F3) — all matches are selected at once;
  persisting the quick-select text across navigation or restart; per-tab status bar instances.
  No Domain/Application change — every count and listing the bar needs already exists on
  `TabViewModel`/`FileListModel`. No automated coverage: pure UI/ViewModel glue, same carve-out
  as the toolbar and address-bar autocomplete (§11).

### 14.31 Window-Wide Status Bar

A persistent, window-wide bar at the very bottom of the application — outside any pane/tab, unlike
`StatusBarWidget` (§14.28, one instance per pane, item/selection counts + quick-select). Shows,
left-aligned, the focused pane's active tab's drive (`"<label> — <free> free of <total>"`) and,
centered, the machine's computer name (`QSysInfo::machineHostName()`, set once — no Port, not
filesystem-related).

* **Reuses `QMainWindow::statusBar()`** rather than a second independent bottom widget — it is
  already bottom-of-window, full-width, and tab-agnostic. `statusBar()` was previously used only
  for `MainWindow::onStatusMessage`'s transient, 5-second auto-hiding failure messages
  (`showMessage()`), which is unchanged. `QStatusBar` keeps "normal" widgets (`addWidget`, hidden
  while a transient message shows) separate from "permanent" widgets (`addPermanentWidget`, never
  hidden) — the new content is added as a single permanent widget with stretch factor 1
  (`statusBar()->addPermanentWidget(m_mainStatusBarWidget, 1)`), so with nothing else competing for
  space it expands to the whole bar width in the common case, only compressing toward the right
  during the rare seconds a transient message is showing.
* **`MainStatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`/`StatusBarWidget`: an internal `[driveLabel, stretch, computerNameLabel,
  stretch]` `QHBoxLayout` (the second stretch both centers the computer name and reserves room for
  a possible future right-hand section) and one setter, `setDriveInfoText(QString)` — empty string
  blanks it. No `TabViewModel`/use-case dependency.
* **`IFileSystemRepository::driveSpaceInfo(path)`** (new Port method) resolves the drive/volume
  containing `path` (any path on it) to a `DriveSpaceInfo{ displayLabel, freeBytes, totalBytes }`
  (new plain-data type, `src/application/DriveSpaceInfo.h`) — `displayLabel` is fully resolved
  UTF-8 text by the adapter (reusing the same `DriveLabel::driveDisplayLabel` helper and
  `GetDriveTypeW`/`GetVolumeInformationW` calls `StandardFileSystemRepository`'s `buildDriveNode()`
  already makes for "This PC", §14.15), so no OS-specific type crosses the Application boundary —
  same posture as `FileNode::displayName()`. `GetDiskFreeSpaceExW` supplies the byte counts.
  Failure (the "This PC" sentinel, an archive-virtual path, or an unreachable volume) is tolerated
  by simply blanking the label, same as `buildDriveNode()`'s `nullopt` case. `FileNavigationUseCase`
  and `TabViewModel` each add a thin passthrough (`TabViewModel::driveSpaceInfo()` calling
  `m_fileNavigationUseCase.driveSpaceInfo(currentPath())`), the same shape as existing
  single-Port-call wrappers (e.g. `showProperties`).
* **Refresh points:** `MainWindow` tracks the focused pane's active tab the same way it already
  does for sort/search (`m_sortTrackedTab`/`m_searchTrackedTab`) with a new
  `m_driveInfoTrackedTab`, connecting its `currentPathChanged` to re-fetch and reformat the drive
  label. Updates on tab-focus change and on navigation only — same event-driven-only posture as
  §14.28's own refresh, no timer/polling.
* **Threading:** kept synchronous on the UI thread, same as `buildDriveNode()`'s identical Win32
  calls during "This PC" enumeration (§14.15) and consistent with §5's accepted "most Port calls
  are still synchronous" gap.
* **Testing:** `driveSpaceInfo()`'s adapter implementation depends on live, per-machine drive
  state exactly like `buildDriveNode()` — covered by the same §11 carve-out ("live drive/known-
  folder enumeration... not covered by automated adapter tests"). The `FileNavigationUseCase`
  passthrough is covered by an ordinary mock-based test, per §11. `DriveLabel` is unchanged/reused
  as-is.
* **Not built:** live/periodic refresh while idle in the same folder; a populated right-hand
  section (layout reserves room for one); non-Windows implementations (a future Linux backend
  implements `driveSpaceInfo` behind the same Port, matching every other Windows-specific adapter
  call); any change to `StatusBarWidget` or `onStatusMessage`'s transient-message behavior.

### 14.32 Git Status Integration (Details View)

Per-file/folder git working-tree status badges in the Details-view file list, computed via
**libgit2** (the plain C library — not libqgit2, which isn't in vcpkg's registry; libgit2 is
wrapped by our own adapter, same posture as libarchive/libvips/FFmpeg/tree-sitter, so "use a
wrapper, never the library directly" is satisfied the same way it already is for those). One status
per file: `??` Untracked, `M` staged/unstaged (Modified), `A` staged (Added), `D` staged/unstaged
(Deleted), `!!` Ignored — seven `GitFileStatus` values total, each mapped to a small colored badge
icon (purple/blue/yellow/green/red/orange/grey respectively) rendered in a new column appended
after Date Modified.

* **New Port**, `IGitStatusProvider` (`src/application`) — kept separate from
  `IFileSystemRepository` (Interface Segregation, same reasoning that already keeps
  `IContextMenuProvider`/`IMediaDecoder` as their own Ports): `statusForDirectory(directory) ->
  Result<GitDirectoryStatus>`, where `GitDirectoryStatus` (`src/application/GitDirectoryStatus.h`,
  a plain Port-adjacent data type, same posture as `DriveSpaceInfo`) holds an optional repository
  root plus a `path -> GitFileStatus` map for `directory`'s immediate children only. `directory` not
  inside any repository is `Ok({nullopt, {}})`, not an error — same "tolerate by blanking" posture
  §14.31 established for `driveSpaceInfo`. **`GitStatusUseCase`** (new, stateless, depends only on
  `IGitStatusProvider&`) is a thin passthrough, same single-Port-dependency shape as
  `FilePreviewUseCase`.
* **`src/adapters/git`** (new module, `explorer_adapters_git`, mirrors `adapters/archive`):
  `LibGit2Repository` — the RAII wrapper around libgit2 itself (the only place its headers are
  included): `open(startDirectory)` discovers a repository walking upward (`Err` if none found —
  satisfies "in the current folder or a subfolder"), `statusForImmediateChildren(relativeDirectory)`
  runs `git_status_list_new` scoped to that directory via `options.pathspec` (so a large repo isn't
  fully re-walked on every navigation) with `GIT_STATUS_OPT_INCLUDE_UNTRACKED |
  GIT_STATUS_OPT_INCLUDE_IGNORED`, `RECURSE_UNTRACKED_DIRS` left off — an entirely-untracked
  subfolder is reported as itself (a single entry) rather than walked file-by-file, a real perf win
  in large repos. Status-flag → `GitFileStatus` mapping has an explicit priority order for a
  partially-staged file (worktree status wins over index status; see the plan record for the full
  list) since req. "one status per file" rules out showing both. `LibGit2StatusProvider` implements
  the Port on top of this, translating a "no repository found" `Err` into `Ok({nullopt, {}})` rather
  than propagating it, and **rolls a nested entry up onto its immediate-child ancestor directory**
  (the first path segment past `directory`) rather than dropping it — a modified file several levels
  deep stays visible without opening every intermediate folder. A folder that rolls up more than one
  nested change keeps its highest-priority one, via a rollup-specific priority order (its own
  ranking, not the flag-collapsing one above): Conflicted, Untracked, DeletedUnstaged,
  ModifiedUnstaged, DeletedStaged, AddedStaged, ModifiedStaged, Ignored last — Ignored sits at the
  bottom here specifically so an ignored build artifact next to a genuinely modified tracked file in
  the same folder doesn't hide the modification behind an "Ignored" badge on the folder row.
* **Threading** — one dedicated background `QThread` (`GitStatusService` +
  `GitStatusWorker`, `src/adapters/viewmodels`), same "one always-alive worker, not a pool" shape as
  `FilePreviewViewModel`/`WindowsConPtyProcess`; every open tab/pane's status requests serialize
  through it, sidestepping any libgit2 thread-safety question by construction (never touched from
  two threads at once) the same way `SQLiteTagRepository` avoids `SQLITE_BUSY` by serializing DB
  access (§5). Dispatch is `QMetaObject::invokeMethod` with a captured functor across both hops, not
  real Qt signals — same reasoning `FilePreviewWorker.h` documents for avoiding meta-type
  registration for non-default-constructible `Result<T>` payloads. `GitStatusService::requestStatus`
  takes a `QObject* context` and guards its reply-marshaling callback with a `QPointer<QObject>`, so
  a request outliving its requester (e.g. a closed tab) is silently dropped rather than dereferencing
  a dangling `this`.
* **Dispatch point is `TabViewModel`, not `FileListModel`** — `FileListModel` gains no new
  constructor dependency, only a plain `setGitStatuses(...)` slot (storing the map, emitting
  `dataChanged` for the new column), keeping it decoupled from git entirely, the same posture
  `resolveDecoration()` keeps it decoupled from `FileDecorationsViewModel`'s concrete type (§14.30).
  `TabViewModel` gains a `GitStatusService&` constructor dependency (threaded through
  `WorkspacePaneViewModel`/`WorkspaceController` exactly like `TagManagementUseCase&`) and dispatches
  a request on every `directoryContentsChanged` — the single funnel point `loadAndApply` already
  provides for every navigation entry point, including the existing `refresh()` used after in-app
  file operations, so a status refresh after e.g. a paste comes for free with no new wiring. Only the
  tab's primary browsing `FileListModel` is wired this way — the two search-result `FileListModel`
  instances never receive `setGitStatuses()`, which is how quick-search/advanced-search are excluded
  (below) with no extra flag.
* **`FileListModel`**: new `Column::GitStatusColumn`, appended after `DateModifiedColumn` (no
  existing column index shifts). "Details view only" falls out for free from the existing
  `QTreeView`/`QListView` split (§2.3.1) — `QListView`'s icon/list/tile modes already only read
  column 0. `data()` returns `{}` (no badge) for any entry not present in the status map; a hit
  returns a cached 16×16 badge icon (`Qt::DecorationRole`, one per `GitFileStatus` value, built
  once) plus a human-readable `Qt::ToolTipRole` string. Synthetic rows are skipped, same as
  decorations.
* **Not built:** status in quick-search/advanced-search result views (those `FileListModel`
  instances span arbitrary directories, which doesn't fit a per-directory status query — a
  deliberate v1 scope cut); rollup past the immediate-child level (a folder's badge reflects
  changes anywhere in its subtree, but a grandchild folder itself gets no separate entry unless
  `directory` is queried directly for it); live/background refresh outside of navigation (no
  filesystem or `.git`-index watcher — matches
  §14.31's "event-driven only" precedent); rename detection (no "R" status in scope); any stage/
  unstage/commit/other write action — read-only display only. No automated coverage for the
  `FileListModel`/`TabViewModel`/`GitStatusService` glue, per the same §11 UI/ViewModel-glue
  carve-out `FilePreviewViewModel`/`DriveSpaceInfoWorker` already fall under; `GitStatusUseCase` is
  covered by an ordinary mock-based test and `LibGit2StatusProvider` by a real-temp-repo adapter
  test (initialized via libgit2's own `git_repository_init`, not a `git` CLI shell-out), per §11.

### 14.33 Live Directory Update (Visible Tabs, Non-Recursive)

Automatically reflects a file being created/deleted/modified in a browsed folder without the user
pressing Refresh — scoped to the tabs currently **shown** (the active tab of each pane the current
`SplitLayout` makes visible: 1 in Single, 2 in a split, 4 in the grid) and to that folder's
**immediate children only**, never a nested subdirectory.

* **New Port**, `IDirectoryWatcher` (`src/application`) — kept separate from
  `IFileSystemRepository`, same Interface Segregation reasoning as `IGitStatusProvider`/
  `IContextMenuProvider`/`IMediaDecoder`: `watch(directory, std::function<void()> onChanged) ->
  Result<DirectoryWatchId>` (multiple independent watches on the same directory are allowed) /
  `unwatch(DirectoryWatchId)`. A non-real path (the "This PC" sentinel, an archive-virtual path, a
  path that no longer exists) surfaces as a `Result` failure rather than crashing; the caller
  simply doesn't get live updates for that tab (same "tolerate by blanking" posture as
  `driveSpaceInfo`, §14.31). **`DirectoryWatchUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `GitStatusUseCase`).
* **`src/adapters/filesystem`** gains `StandardDirectoryWatcher` (implements the Port on top of
  one owned `QFileSystemWatcher` — chosen over hand-rolling `ReadDirectoryChangesW` because it's
  already non-recursive per directory by construction, and its Windows backend watches
  name/attribute/size/last-write changes of immediate children, covering create/delete/rename/
  modify for free; it also gives a future Linux port (inotify backend) for free) and
  `DirectoryWatchRegistry` (pure C++, no Qt — refcounted path→watcher-callbacks bookkeeping
  deciding when to actually start/stop the native watch — same "pure logic factored out of OS
  glue" split as `AnsiTerminalBuffer`/`WindowsConPtyProcess`, GTest-covered). Path conversion goes
  through `.wstring()` → `QString::fromStdWString(...)`, never `.string()`/`PathUtf8::toUtf8()`
  (§6). This is `adapters/filesystem`'s first Qt dependency (`Qt6::Core`, for
  `QFileSystemWatcher`/`QObject`) — `StandardFileSystemRepository`/`DriveLabel` use plain
  `std::filesystem`/Win32 only.
* **`DirectoryWatchService`** (new, `src/adapters/viewmodels`) — owned long-lived by
  `WorkspaceController` (constructed internally, same posture as `TagListViewModel`/
  `FilePreviewViewModel`/`FileOperationsController`). `setVisibleTabs(tabs)` diffs against the
  currently-watched set: newly-visible tabs are watched *and* refreshed once immediately to catch
  up on anything missed while unwatched (mirrors `FilePreviewViewModel::setPanelActive(true)`'s
  reactivate-and-catch-up posture); no-longer-visible tabs are unwatched. Each watched tab tracks
  its `DirectoryWatchId`, a ~400ms debounce `QTimer` (coalesces a burst of native change events —
  e.g. a multi-file copy — into one refresh), and connections to that tab's `currentPathChanged`
  (re-point the watch at the new directory, same tab stays watched) and `destroyed()` (cleanup on
  tab close). A debounced notification calls the existing `TabViewModel::refresh()` slot (§14.10)
  — no new refresh path. Only the normal-browsing `FileListModel` is affected; a watched tab
  currently showing quick/advanced-search results still refreshes its (hidden) primary listing in
  the background, same scope cut §14.32 already made.
* **`WorkspaceController`** gains a `DirectoryWatchUseCase&` constructor parameter and a private
  `updateLiveWatchedTabs()` (`WorkspaceLayoutTopology::visiblePanes(m_layout)` → each visible
  pane's `activeTab()` → `DirectoryWatchService::setVisibleTabs`), called after construction, from
  `setLayout()`, and from the existing per-pane `activeTabChanged` connection loop — deliberately
  **not** from `focusedPaneChanged`, since visibility (not focus) decides what's watched: all 4
  panes' active tabs live-update under `FourGrid` even though only one pane has focus.
* **Threading** — none new. Registering/unregistering a directory watch is a fast, non-blocking
  syscall, same "cheap OS call stays synchronous on the UI thread" posture as `driveSpaceInfo`
  (§14.31)/`ShellExecuteW` (§14.11); `QFileSystemWatcher` delivers notifications through Qt's own
  event loop with no manual thread. Unlike git-status (a real disk/`.git` scan, needing
  `GitStatusService`'s background thread), there is no scan to offload here.
* **Known limitation:** `refresh()`'s `loadAndApply()` already clears the current selection and
  fully resets `FileListModel` (`beginResetModel()`/`endResetModel()`) on every call — pre-existing
  behavior for the manual Refresh button and the post-file-op refresh (§14.10), now also triggered
  by external changes, more frequently and more disruptively than before (Explorer itself
  preserves selection across a live update). Selection-preserving refresh is a real improvement but
  a separate, larger change to `FileListModel`/`loadAndApply`; deliberately out of scope here.
* **Not built:** watching inactive tabs or hidden-pane tabs (they simply go stale until shown or
  manually refreshed); live updates inside quick-search/advanced-search result views; a Linux
  `IDirectoryWatcher` implementation (deferred until Linux support is actually built, same posture
  as every other Windows-first Port). No automated coverage beyond `DirectoryWatchRegistry`
  (pure bookkeeping) and `DirectoryWatchUseCase` (mocked-Port passthrough) — `WorkspaceController`/
  `DirectoryWatchService`/`StandardDirectoryWatcher` fall under the existing §11 UI/ViewModel-glue
  and OS-shell carve-outs, smoke-tested manually.

### 14.34 Folder Monitoring (Recursive, Per-Tab)

A third right-panel tab, "Monitoring" (§14.25's panel already supports more than one tab with no
further plumbing), that recursively watches one tab's current directory (including every
subdirectory) for create/delete/modify and shows a live, colored event log plus a running total —
primarily meant for watching a long-running build (Specification.md §2.1). Deliberately a separate
Port/adapter from `IDirectoryWatcher`/`StandardDirectoryWatcher` (§14.33) rather than an extension of
it: that watcher is intentionally non-recursive and reports no path or change kind (just "re-list this
folder"), whereas this feature needs full-subtree coverage plus a classified, path-carrying event per
change — `QFileSystemWatcher` (§14.33's backend) cannot deliver either.

* **`src/domain/PathContainment.h`/`.cpp`** (new, pure, no framework deps) — `isPathWithinOrEqual
  (candidate, root)`, a lexical is-subpath check via `path::lexically_relative` (never narrows to
  `std::string`, §6-safe for non-ASCII paths). Backs the navigation guard below; GTest-covered
  (`tests/domain/PathContainmentTest.cpp`).
* **New Port**, `IFolderMonitor` (`src/application`) — `startMonitoring(rootDirectory,
  std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
  std::function<void(Result<FolderMonitorId>)> onStarted) -> void` / `stopMonitoring(FolderMonitorId)`.
  Unlike most other Ports, `startMonitoring` cannot report success/failure via a return value: opening
  the directory handle is disk I/O that must never run on the calling (UI) thread, so `onStarted` is
  invoked exactly once instead, already marshaled onto the calling thread the same way `onChanged`'s
  batches are — a "This PC" sentinel or archive-virtual root still fails the same way
  `IDirectoryWatcher::watch` does, just reported through `onStarted` rather than a synchronous
  `Result`. `FolderChangeEvent` (`src/application/FolderChangeEvent.h`, plain data, same posture as
  `DriveSpaceInfo`/`GitDirectoryStatus`) holds a root-relative path, a `FolderChangeKind`
  (`Added`/`Removed`/`Modified`), and a best-effort `isDirectory` flag. **`FolderMonitorUseCase`** (new,
  stateless, single-Port-dependency passthrough, same shape as `DirectoryWatchUseCase`) forwards
  `onStarted` unchanged.
* **`StandardFolderMonitor`** (`src/adapters/filesystem`, new, Windows-only implementation behind the
  cross-platform-named Port, same "Windows first" posture as `StandardDirectoryWatcher`/
  `ShellContextMenuProvider`): one background `std::thread` per active monitor, spawned from
  `startMonitoring` itself so that `CreateFileW` (opening the directory handle) also never runs on the
  calling thread — only the "This PC" sentinel check stays synchronous (pure logic, no I/O). The
  thread reports the open's outcome via `onStarted` (marshaled back the same way `onChanged`'s batches
  are) and then, on success, blocks on `ReadDirectoryChangesW` (`bWatchSubtree = TRUE`) — the same "one
  background thread per resource, results marshaled back via
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`" shape `WindowsConPtyProcess` already
  established for its ConPTY reader thread. A successful `GetOverlappedResult` reporting zero bytes is
  ReadDirectoryChangesW's documented buffer-overflow signal (too many changes for the fixed 16KB
  buffer in one cycle) — treated as "some changes lost, keep watching" (re-issue the read) rather than
  a fatal error. A small in-memory path→isDirectory index, seeded by one initial recursive scan (also
  on the worker thread) and kept current as Added/Modified events arrive, lets a Removed event still
  report `isDirectory` (an already-deleted path can't be `stat()`-ed) — an accepted heuristic (falls
  back to "file" on a miss), same posture as the file-identity hash (§8) or `ArchivePathResolver`'s
  nested-archive limitation. Rename events (`FILE_ACTION_RENAMED_OLD_NAME`/`NEW_NAME`) fold into
  `Removed`/`Added` — no dedicated "renamed" kind. The directory handle is opened with
  `FILE_FLAG_OVERLAPPED`, and each monitor also holds a manual-reset stop `HANDLE`: the worker thread
  waits on `WaitForMultipleObjects` over the pending read's `OVERLAPPED` event and the stop event,
  since closing the handle alone does not reliably interrupt a synchronous `ReadDirectoryChangesW`
  from another thread (a documented Win32 hang hazard). `stopMonitoring` signals the stop event, then
  hands the actual `join()` + handle/event cleanup off to its own detached throwaway thread — waiting
  for `CancelIoEx`/`GetOverlappedResult` to resolve against an unresponsive network share's redirector
  must not block the calling (UI) thread either. The destructor still joins synchronously (a brief
  stall during app shutdown is acceptable, unlike during interactive use) for every monitor still
  open. Non-Windows builds fail every call (via `onStarted`) with `ErrorCode::IoError`. Falls under the
  existing OS-shell/live-notification carve-out (§11) — no automated adapter test, smoke-tested
  manually, same as `StandardDirectoryWatcher`/`WindowsConPtyProcess`.
* **Per-tab state on `TabViewModel`**, not a separate global monitor — the same "state lives on the
  tab" posture already used for sort/search/advanced-search (§14.12/§14.18/§14.19), so two different
  tabs can independently monitor at once. New `FolderMonitorUseCase&` constructor dependency (threaded
  through `WorkspacePaneViewModel`/`WorkspaceController`/`CompositionRoot` exactly like
  `GitStatusService&`); state = active flag, monitored root, a capped-200 ring-buffer log
  (`std::deque<FolderChangeEvent>`), an uncapped running total, and a pinned flag. `startMonitoring()`/
  `stopMonitoring()` slots; stopping also clears the pinned flag (nothing left to keep pinned) but
  deliberately leaves the log/total as-is until the next `startMonitoring()` clears them for a fresh
  session — so a finished build's final tally stays visible until the user starts watching again.
  Since `IFolderMonitor::startMonitoring`'s open is now async (see above), `isMonitoringActive()` only
  flips true once `onStarted` reports success — a `m_monitoringStarting` flag guards against a
  redundant second dispatch during that gap, and `stopMonitoring()` called during that gap sets
  `m_monitoringCancelled` so the eventual `onStarted` callback stops the monitor immediately instead of
  activating it. That callback captures a `QPointer<TabViewModel>` guard (not `this` directly) plus a
  raw `FolderMonitorUseCase*`, so if the tab is closed while the open is still in flight, a
  since-orphaned successful open is stopped via the pointer rather than touching the destroyed tab.
  Also gains three pure navigation-guard queries: `upTarget()` (the parent-or-ThisPC-sentinel `goUp()`
  already computes internally, now exposed so `goUp()` itself becomes `navigateTo(upTarget())`) and
  `peekBack()`/`peekForward()` (backed by two new non-mutating peek methods on `NavigationHistory`).
* **Navigation/close guard, entirely in `WorkspacePaneWidget`** (never a callback threaded into
  `TabViewModel` — same "confirm in the widget, then call the ViewModel" posture
  `onDeleteRequested`'s `QMessageBox::question` already establishes, §14.10): a shared
  `confirmLeavingMonitoredScope(tab, targetPath)` helper, built on `isPathWithinOrEqual`, gates every
  navigation call site that can move a tab's path — the toolbar Back/Forward/Up actions and
  `FileBrowserView`'s equivalent keyboard signals (consolidated into three shared slots, removing
  previously-duplicated lambdas), double-click/Enter into a folder, and the address bar — stopping
  monitoring and proceeding on "Yes", leaving the tab exactly where it was on anything else. Note this
  generalizes the product requirement's literal "moves to a parent directory" to "navigates outside
  the monitored subtree" (any sibling/unrelated/drive/This-PC target), since a narrower literal
  reading would let the same warning be trivially bypassed via the address bar or Back/Forward.
  Closing a monitored tab (`QTabWidget::tabCloseRequested`) goes through an equivalent confirm-first
  `onTabCloseRequested` slot instead of calling `WorkspacePaneViewModel::closeTab` directly;
  `WorkspacePaneViewModel::closeTab` itself also calls the tab's `stopMonitoring()` right before
  `deleteLater()` as teardown hygiene. `MainWindow::closeEvent` carries an equivalent whole-workspace
  guard: before saving config and accepting the close, it walks every pane
  (`WorkspacePaneId::PaneA..PaneD`, regardless of which are laid out under the current `SplitLayout`)
  and every tab in each, and if any `TabViewModel::isMonitoringActive()`, prompts with the same
  `QMessageBox::question` pattern before allowing the application to exit; declining calls
  `event->ignore()` and leaves the window and all monitoring sessions untouched.
* **`MonitoringPanelViewModel`** (new, `src/adapters/viewmodels`) — the shared ViewModel behind the
  panel, owned by `WorkspaceController` and retargeted from `retargetFocusedTab()` alongside
  `TagListViewModel`/`FilePreviewViewModel`, but with no Port dependency of its own (all real state
  lives on `TabViewModel` above) — purely a retargeting/forwarding shim. Its `setFocusedTab(tab)` is a
  no-op whenever the currently-displayed tab's monitoring is pinned, which is what makes "pin" keep
  the panel locked onto that tab's session regardless of which pane/tab actually has focus
  (Specification.md §2.1) — otherwise it switches and re-emits the new tab's current
  active/log/total/pinned state, the same disconnect-old/connect-new/refresh-now shape
  `TagListViewModel::setActiveTab` already uses.
* **`MonitoringLogModel`** (new, `src/ui/widgets`, mirrors `FolderPreviewListModel`'s lazy-decoration
  posture) — a `QAbstractListModel` over the displayed tab's ring buffer; a generic file-or-folder icon
  keyed only by each event's `isDirectory` flag (never a per-row shell-icon lookup — a deleted entry
  can't be resolved to one anyway, and a fast-scrolling build log shouldn't pay for one per row even
  for entries that still exist) and a foreground color keyed by `FolderChangeKind` (new
  `UiColors::kMonitorAddedGreen`/`kMonitorRemovedRed`/`kMonitorModifiedYellow` constants). Incremental
  `beginInsertRows`/`beginRemoveRows` on append (not a full reset per event) keeps a busy monitor
  cheap. **`MonitoringPanelWidget`** (new, `src/ui/widgets`) — Start/Stop `QPushButton` plus a
  checkable pin `QToolButton` on the same row (enabled only while monitoring is active), a `QListView`
  over `MonitoringLogModel` that calls `scrollToBottom()` on every append, and a `Total: <N>`
  `QLabel` — wired to `MonitoringPanelViewModel`'s signals the same way `TagPanelWidget` wires to
  `TagListViewModel`'s. `MainWindow::createWorkspace()` adds it as a third `RightPanelWidget` tab next
  to "Tags"/"Preview" — no `RightPanelWidget` change needed.
* **Not built:** a Linux `IFolderMonitor` implementation (deferred until Linux support is actually
  built); a dedicated "renamed" event kind/color; monitoring-session persistence across app restart
  or across a Stop→Start cycle beyond "Stop freezes the log/total, Start clears them," matching the
  terminal/bottom-panel's existing unpersisted posture (§14.24); a global one-monitor-at-a-time cap
  (multiple tabs may monitor concurrently by design); real per-file OS shell icons in the log rows;
  monitoring a "This PC" or archive-virtual path
  (rejected, same as `IDirectoryWatcher::watch`); any UI for reviewing/exporting a session's full
  history beyond the live 200-row window plus the running Total. No automated coverage beyond
  `PathContainment` (pure) and `FolderMonitorUseCase` (mocked-Port passthrough) — `TabViewModel`'s new
  state, `NavigationHistory`'s two peek additions aside, `MonitoringPanelViewModel`,
  `MonitoringLogModel`, `MonitoringPanelWidget`, `StandardFolderMonitor`, and the
  `WorkspacePaneWidget` guard wiring fall under the existing §11 UI/ViewModel-glue and OS-shell
  carve-outs, smoke-tested manually.

### 14.35 Git Branch Status and Checkout (Per-Pane Status Bar)

Extends the per-pane status bar (§14.28) with a repository-level git indicator, distinct from
§14.32's per-file Details-view badges: whenever the focused tab's current folder is inside a git
working tree, the status bar's center shows the current branch name (or a short commit id when
`HEAD` is detached) next to a plain count of the repository's changed files; clicking the branch
name opens a menu of local branches and checks out the one the user picks.

* **New Port**, `IGitBranchProvider` (`src/application`) — kept separate from `IGitStatusProvider`
  (Interface Segregation, same split reasoning as `IFolderMonitor`/`IDirectoryWatcher`): that Port's
  callers are all read-only per-file badge consumers, and have no business depending on a Port that
  also exposes a write operation (`checkoutBranch`). Three methods: `repositorySummary(directory) ->
  Result<GitRepositorySummary>` (tolerate-by-blanking when `directory` isn't inside a repository,
  same posture as `IGitStatusProvider::statusForDirectory`), `listLocalBranches(directory) ->
  Result<vector<string>>` (local branches only, no remotes), and `checkoutBranch(directory,
  branchName) -> Result<void>` (a **safe**, non-forced checkout — fails rather than discarding
  changes if it would overwrite a modified tracked file, matching plain `git checkout`'s default
  safety; no `--force` path exists). `GitRepositorySummary` (`src/application/
  GitRepositorySummary.h`, plain Port-adjacent data, same posture as `GitDirectoryStatus`) holds an
  optional `repositoryRoot`, a `headDisplayName` (branch short name, or a short commit id when
  detached, or the would-be branch name pre-first-commit on an unborn branch), an `isDetached` flag,
  and a repo-wide `changedFileCount`. **`GitBranchUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `GitStatusUseCase`).
* **`LibGit2Repository` gains four methods** (`src/adapters/git`, the same RAII wrapper §14.32
  introduced): `headDisplayName(bool& isDetachedOut)` (`git_repository_head` for the normal case;
  `GIT_EUNBORNBRANCH` falls back to reading raw `HEAD`'s symbolic target via `git_reference_lookup
  (..., "HEAD")` so a freshly-`git init`'d repo with no commits still shows its would-be branch
  name, matching plain `git status`'s pre-first-commit wording); `changedFileCount()` (reuses
  `statusForImmediateChildren("")` at the workdir root — already generically "whole repo" when given
  an empty pathspec — filtering out `Ignored` and counting the rest, so an entirely-untracked
  directory still costs one entry rather than a per-file walk, the same trade-off that method's
  existing doc comment already accepts); `listLocalBranches()` (`git_branch_iterator_new
  (GIT_BRANCH_LOCAL)`); `checkoutBranch(branchName)` (`git_branch_lookup` + `git_reference_peel` to
  the target commit + `git_checkout_tree` with `GIT_CHECKOUT_SAFE` — never `FORCE` — + `git_
  repository_set_head` to `refs/heads/<branchName>`; any libgit2 failure, including a safe-checkout
  conflict, is surfaced as `Result::failure` with HEAD/the working tree left untouched).
  **`LibGit2BranchProvider`** (new, implements `IGitBranchProvider`) opens a fresh `LibGit2Repository`
  per call exactly like `LibGit2StatusProvider`, no repository/connection cache.
* **Reuses the existing single-worker-thread posture, not a second thread** — `GitStatusService`/
  `GitStatusWorker` (§14.32) gain a `GitBranchUseCase&` dependency alongside their existing
  `GitStatusUseCase&` and three new request methods (`requestRepositorySummary`,
  `requestLocalBranches`, `requestCheckout`, same `QObject* context` + `QPointer`-guarded-callback
  shape as `requestStatus`), all dispatched onto the *same* `m_workerThread` `requestStatus` already
  uses — deliberately not a second dedicated thread, since the entire point of serializing through
  one worker was "sidestepping any libgit2 thread-safety question by construction" (§14.32), and
  that guarantee is strongest when every libgit2 call in the process, badges or branch/checkout,
  funnels through the one thread.
* **Dispatch and caching on `TabViewModel`, not `FileListModel`** — same funnel point §14.32
  established: the `directoryContentsChanged` handler that already calls `requestGitStatus(path)`
  now also calls a new `requestGitRepositorySummary(path)`, so a repo-wide summary refresh comes for
  free on every navigation and on `refresh()` (including the refresh a completed checkout below
  triggers) with no new wiring. The latest summary is cached on the tab
  (`lastGitRepositorySummary()`) so switching tabs shows the right branch/count immediately from
  cache, the same "read the cached value immediately, let the async refresh correct it later" shape
  `refreshStatusCounts()` already uses for object counts. New signals: `gitRepositorySummaryChanged
  (const GitRepositorySummary&)` and `gitOperationFailed(const QString&)`. Two new public methods:
  `requestLocalBranches(callback)` (a thin forward to `GitStatusService::requestLocalBranches` scoped
  to the cached `repositoryRoot`, no-op returning an empty list when the tab isn't in a repository)
  and `checkoutBranch(branchName)` (forwards to `GitStatusService::requestCheckout`; a no-op if
  `branchName` is already the current, non-detached `HEAD`; on success calls the tab's own
  `refresh()`, which re-navigates the current folder and — via the funnel above — re-fetches both the
  file list and the git status/summary for the new `HEAD`; on failure emits `gitOperationFailed`
  with the `Result`'s error message, following the same `QString::fromStdString(result.error()
  .message)` convention every other ViewModel failure signal already uses).
* **`StatusBarWidget`** (`src/ui/widgets`, §14.28) gains a small `QToolButton` (branch name, hidden
  when the tab isn't in a repository) immediately followed by a plain, non-clickable `QLabel`
  showing only `changedFileCount`, inserted between the existing left-aligned counts label and the
  stretch before the quick-select field so the pair renders centered in the bar. New members: slot
  `setGitInfo(bool inRepository, const QString& headDisplayName, int changedFileCount)`; signal
  `branchButtonClicked()` (emitted on click, no menu attached yet — same "plain display widget,
  no ViewModel dependency, `WorkspacePaneWidget` mediates" posture as `AddressBarWidget`'s
  `folderSuggestionsRequested`, §14.21); method `showBranchMenu(const QStringList& branchNames,
  const QString& currentBranchName)` (builds and pops up a `QMenu` at the button's position, one
  checkable `QAction` per branch — checked for `currentBranchName` — and re-emits the chosen one via
  signal `branchSelected(const QString&)`). `WorkspacePaneWidget::bindToolBarToTab` additionally
  (dis)connects the newly-bound tab's `gitRepositorySummaryChanged` to a `updateGitStatusBar(const
  GitRepositorySummary&)` slot (which calls `m_statusBar->setGitInfo(...)`, applying the cached
  value immediately on bind, same as the counts refresh) and `gitOperationFailed` to a new
  `WorkspacePaneWidget::gitOperationFailed(const QString&)` signal, wired in `MainWindow` alongside
  the existing per-pane `navigationFailed` → `onStatusMessage` connections (§14.14's error-surfacing
  convention). `WorkspacePaneWidget` itself wires `m_statusBar`'s `branchButtonClicked` to call
  `m_boundTab->requestLocalBranches(...)` and, on the async result, `m_statusBar->showBranchMenu
  (...)`; and `branchSelected` to `m_boundTab->checkoutBranch(branchName)`.
* **Not built:** remote branches in the menu (local only, matching the product ask); creating a new
  branch or any other write action besides checkout; a confirmation dialog before checkout (the menu
  click itself is the deliberate action, and a conflicting checkout already fails safely without
  touching the working tree); force-checkout/discard-changes recovery UI when a checkout is refused;
  live/background refresh of the branch indicator outside of navigation (matches §14.32/§14.31's
  "event-driven only" precedent — a checkout performed outside the app, e.g. from a terminal, isn't
  picked up until the next navigation or manual refresh); remembering which branch was checked out
  across restarts; any UI for an unborn/headless repository beyond showing its would-be branch name.
  No automated coverage for the `TabViewModel`/`StatusBarWidget`/`WorkspacePaneWidget` glue (§11's
  existing UI/ViewModel-glue carve-out); `GitBranchUseCase` gets an ordinary mocked-Port test and
  `LibGit2Repository`'s four new methods / `LibGit2BranchProvider` get a real-temp-repo adapter test
  (via libgit2's own `git_repository_init`/`git_branch_create`, not a `git` CLI shell-out), per §11,
  including a checkout-refused-by-conflicting-changes case.

