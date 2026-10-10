# Architecture Document — Feature Decision Log: Monitoring, Git & Productivity Features

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** (§14.44 and §14.59 are the two
exceptions: each adds a small, additive field/helper to an existing Domain/Application type rather
than redesigning either layer's existing shape) — and is recorded as a decision log per feature, not
an implementation walkthrough; see git history and the code's own comments (many cite these
subsection numbers directly) for the detail.

Covers §14.34–§14.59. Preceded by
[01-workspace-navigation-and-search.md](01-workspace-navigation-and-search.md) (§14.1–§14.26)
and [02-preview-decoration-and-status.md](02-preview-decoration-and-status.md) (§14.27–§14.33).

---

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

### 14.36 Favorites

User-curated shortcuts to files/folders, organized into optional categories, managed from a new
`Favorites` top-level menu (Specification.md §2.1). Like §14.30's per-file/folder color rules or
§14.27's highlight theme, this is a persisted UI preference — **no Domain or Application layer
change**, same posture as every other entry in this section.

* **`src/adapters/favorites`** (new module, `explorer_adapters_favorites`, mirrors
  `adapters/decoration`, links only `explorer_domain`): `FavoriteItem` (`Id` = `std::int64_t`, mirrors
  `Tag::Id`; `path`, display text derived from `path.filename()` at render time, no stored display
  name) and `FavoriteCategory` (`Id`, `name`, an ordered `std::vector<FavoriteItem>`). `Favorites`
  owns one ordered top-level `std::vector<std::variant<FavoriteItem, FavoriteCategory>>` plus an
  internal monotonic id counter, with a pure mutation API (add/remove/rename/reorder/move-between-
  categories) — no Qt, no I/O. Deleting a category flattens its items back to top-level rather than
  deleting them (non-destructive by default, matching this app's no-undo-in-v1 posture,
  Specification.md §5). Adding an already-favorited path is a no-op (dedup by path,
  case-insensitively — `PathContainment::pathsEqualCaseInsensitive`, matching Windows' own
  case-insensitive filesystem semantics).
* **`src/adapters/config`** gains `FavoritesStore` (tolerant `load()`/atomic `save()` via the
  existing `JsonFileStore` helpers, own `favorites.json`, §9) and `FavoritesViewModel` — the same
  shape §14.30 established for `FileDecorationRulesStore`/`FileDecorationsViewModel`, including why
  these live in `adapters/config` rather than `adapters/viewmodels`: `adapters/config` already depends
  on `adapters/viewmodels`, so the reverse edge would be a build-graph cycle. A mutation applies to
  the live `Favorites` synchronously, emits `favoritesChanged()`, and queues the actual disk save onto
  the worker thread via an `AsyncSaveDispatcher<Favorites>` member (Architecture.md §5; see the
  `AsyncSaveDispatcher`/`AsyncSaveWorker` note below — the generic replacement for what used to be a
  hand-rolled `FavoritesSaveWorker`). `CompositionRoot` owns the usual store→model→viewmodel triple
  and exposes `favoritesViewModel()`.
* **Selecting an entry after navigation (new capability)** — nothing previously let a caller navigate
  a tab *and* select a specific resulting row; the only prior use of
  `FileBrowserView::selectEntryByPath` (now made public) was Details-view arrow-key navigation, called
  synchronously right after its triggering signal since navigation is still synchronous on the UI
  thread (§5). New `WorkspacePaneWidget::navigateAndSelect(directory, optional entryToSelect)` routes
  through the same `confirmLeavingMonitoredScope` guard every other navigation entry point already
  uses, navigates, then selects if requested and the navigation landed.
* **Menu wiring, `MainWindow`** — fleshes out the previously-empty `Favorites` menu stub: "Add to
  Favorites" (targets the focused tab's selection, or its current browsed folder if nothing is
  selected — same fallback §14.9's tag panel already established; disabled with no focused tab or on
  the `VirtualPaths::ThisPC` sentinel), "Manage Favorites..." (opens `ManageFavoritesDialog`, same
  `exec()`-a-modal shape as `TagManagerDialog`), a separator, then the live entries — rebuilt lazily
  on the menu's `aboutToShow` (a plain `QAction` per uncategorized item, a `QMenu` submenu per
  category populated the same way) rather than kept continuously in sync, so no extra
  `favoritesChanged()` wiring is needed in `MainWindow` itself. Clicking a leaf resolves file-vs-folder
  via the existing `FileNavigationUseCase::stat()` passthrough: a folder navigates the focused pane's
  active tab into it; a file navigates to `path.parent_path()` and selects the file there (product
  decision — folder favorites behave like Explorer Quick Access, not "reveal in parent").
* **`ManageFavoritesDialog`** (new, `src/ui/widgets`) — a `QTreeWidget`-based dialog wired directly to
  `FavoritesViewModel` (no separate dialog-specific ViewModel needed), mirroring `TagManagerDialog`'s
  thin-dialog-over-a-ViewModel shape but using a two-level tree (category → items, native
  expand/collapse covering the "collapsible sections" requirement) instead of `TagManagerDialog`'s
  flat `FlowLayout` of chips. Toolbar: Add File.../Add Folder... (`QFileDialog`, since Qt has no
  single native picker for "either a file or a folder"), Add Category, Rename (category rows),
  Move to Category... (item rows), Move Up/Move Down (enabled by sibling position at the row's own
  level, same boundary-check shape `SettingsDialog::refreshDecorationRows()` already uses for its
  decoration-rule up/down buttons), Remove, Close. Reordering is button-based, not drag-and-drop —
  matching this codebase's existing ordered-list-editing precedent (§14.30) rather than introducing a
  new interaction pattern. Rebuilds the whole tree on `favoritesChanged()`, same "re-render on any
  change" posture `TagManagerDialog::rebuildTagChips` already uses.
* **Not built:** drag-and-drop add (from the file browser onto the Favorites menu) or a right-click
  "Add to Favorites" context-menu entry — menu-only entry point in this pass; drag-and-drop reordering
  in the Manage dialog; a keyboard shortcut for "Add to Favorites"; broken-favorite detection/
  indicator or any periodic re-check of a favorited path's existence; import/export of the favorites
  list; nested categories (one flat level, matching the "collapsible sections" requirement, not a
  category tree). No automated coverage for `FavoritesViewModel`/`ManageFavoritesDialog`/the
  `MainWindow`/`WorkspacePaneWidget` menu and navigate-and-select glue, per the existing §11
  UI/ViewModel-glue carve-out (same as `FileDecorationsViewModel`/`TagManagerViewModel`/
  `TagManagerDialog`) — smoke-tested manually instead. `Favorites`' pure mutation logic and
  `FavoritesStore`'s JSON round-trip/tolerant-load behavior each get an ordinary GTest suite under
  `tests/adapters`, mirroring `FileDecorationRulesTest.cpp`/`FileDecorationRulesStoreTest.cpp`.

### 14.37 Notification System

App-wide toast/progress notifications, `Settings → General → Notifications` (Specification.md
§2.1), and the first two call sites (tag applied/removed). Like §14.27/§14.30/§14.36, this is a
pure presentation concern: **no Domain/Application change, no new Port, no `CompositionRoot`
use-case wiring**.

* **Two kinds.** *Popup* notifications (Info/blue, Warning/yellow, Error/red) auto-dismiss after a
  per-type timeout and always have a manual close button; stack bottom-right, newest nearest the
  corner. *Progress-bar* notifications are a factory API — `createProgressNotification(title,
  barCount, cancellable = false)` returns an id; client code asynchronously updates a bar's
  current/max/above-text/below-text or terminates the whole notification via that id, and an
  optional Cancel button surfaces a `progressCancelRequested(id)` signal back to whoever owns that
  id. **Wired to the custom copy/move pipeline as of §14.47** — see that section; the mechanism
  remains generic for any future long-running operation to call into.
* **`src/adapters/notifications`** (new module, `explorer_adapters_notifications`, links only
  `explorer_domain` + `Qt6::Core` — sits *below* `adapters/viewmodels`, deliberately not in
  `adapters/config`): `NotificationType` (plain enum); `NotificationSettings` (plain C++, no Qt — a
  fixed `{enabled, timeoutMs}` per type, not a sparse override map, since there are always exactly
  3 rows; `timeoutMs == 0` means "never auto-dismiss, close button only"); `NotificationCenter`
  (`QObject`) — the single shared dispatcher every feature calls into, holding a `const
  NotificationSettings&` so `showPopup` can check enabled/timeout synchronously with zero
  dependency on `adapters/config`. Qt's signal/slot auto-connection already queues delivery across
  threads by the *receiver's* affinity, the same property `WindowsConPtyProcess` relies on
  (§14.24), so `updateProgressBar`/`terminateProgressNotification` are safe to call from a future
  background worker thread with no extra marshaling here.
* **Why not `adapters/config`:** that module already depends on `adapters/viewmodels` (§14.30's own
  note re: `AppConfig`/`FileListModel::ColumnCount`). `TagListViewModel` (in `adapters/viewmodels`)
  needs to call into the notification dispatcher, so putting it in `adapters/config` would be the
  same `viewmodels → config → viewmodels` cycle §14.30 already solved once — solved the same way
  here: the plain data (`NotificationSettings`) lives below `adapters/viewmodels`; only the Qt
  settings-page wrapper below lives in `adapters/config`, wired only into `MainWindow`/
  `SettingsDialog`, never into a ViewModel.
* **`src/adapters/config`** gains `NotificationSettingsStore` (own `notification_settings.json`,
  §9, same tolerant-load/atomic-save shape as `HighlightThemeStore`), an
  `AsyncSaveDispatcher<NotificationSettings>` member (background-thread save), and
  `NotificationSettingsViewModel` (`settingsFor`/`setSettingsFor`,
  `settingsChanged()`) — the same three-piece shape §14.27/§14.30 established, mutating the *same*
  `NotificationSettings` instance `NotificationCenter` holds a `const&` to (both owned by
  `CompositionRoot`), so a Settings-page edit is visible on `NotificationCenter`'s very next call
  with no signal wiring between the two.
* **`WorkspaceController`/`TagListViewModel`** each gain a trailing `NotificationCenter&`
  constructor parameter (same threading style as `GitStatusService&`/`FolderMonitorUseCase&`).
  `TagListViewModel::addTagToSelection`/`removeTagFromSelection` fire an Info popup ("Tag Applied"/
  "Tag Removed", naming the tag and the target) after a successful assign/unassign — the first two
  call sites; the mechanism is generic so a future feature can raise Warning/Error the same way.
* **`src/ui/widgets`:** `ToastNotificationWidget` (one popup card: colored header icon+type name,
  body text, close button, an auto-dismiss `QTimer` skipped when `timeoutMs == 0`);
  `ProgressNotificationWidget` (title + N rows, each a label *above* and a smaller-font label
  *below* one `QProgressBar`, plus an optional Cancel button when constructed `cancellable` — see
  §14.47; still no manual close otherwise, ends only via `terminateProgressNotification`);
  `NotificationOverlayWidget` (a plain child of `MainWindow`, always raised, anchored bottom-right
  via a `resizeEvent` override, stacking the cards above in a bottom-aligned layout).
  `SettingsDialog` gains a `General → Notifications` category: a fixed 3-row table (Type / Enabled
  checkbox / Timeout spinbox in seconds), immediate-apply like every other Settings page — no
  Add/Remove, unlike the Colors page's editable rule list, since the rows are fixed. `UiColors.h`
  gains `kNotificationInfoBlue`/`kNotificationWarningYellow`/`kNotificationErrorRed`.
* **Not built:** a cap on simultaneously visible notifications/overflow scrolling (unbounded
  stacking accepted); sound; click-to-action; a history/log of dismissed popups; persisting
  in-flight notifications across restart (only the 3-row settings persist). No automated coverage
  for `NotificationCenter` or any widget above beyond the existing §11 UI/ViewModel-glue carve-out;
  `NotificationSettings`'s plain logic and `NotificationSettingsStore`'s JSON round-trip get
  ordinary GTest coverage under `tests/adapters`.

### 14.38 Async Advanced Search

Moves Advanced Search's (§14.19/§14.20) two I/O-bound operations — the non-tag recursive folder scan
and the tag-first candidate fetch (tag DB query + per-path `stat()`) — off the UI thread, so a large
folder or a heavily-tagged result set no longer freezes the app while it runs. No Domain/Application
change and no new Port: `AdvancedSearchWorker` calls the same `FileNavigationUseCase`/
`TagManagementUseCase` methods `TabViewModel::runAdvancedSearch` already called synchronously, just
from a different thread.

* **`AdvancedSearchWorker`** (`adapters/viewmodels`, plain `QObject` moved to a dedicated `QThread`) —
  same shape as `GitStatusWorker`/`FilePreviewWorker`. `scanRecursive(root, onProgress = nullptr)`
  wraps `FileNavigationUseCase::listDirectoryRecursive`, forwarding `onProgress` straight through;
  `fetchTagFirstCandidates(root, tagIds)` wraps `TagManagementUseCase::findFilesWithAllTags` + the
  per-path `isPathWithinOrEqual`/`stat()` loop §14.20 already established, returning the raw
  candidate `FileNode`s. `filterByCriteria`/`sortBy` stay on the UI thread in both cases — pure,
  cheap, no I/O, no reason to cross threads for them.
* **Progressive results from the recursive scan.** `IFileSystemRepository::listDirectoryRecursive`
  takes an optional `onProgress` callback (`const std::vector<FileNode>&`), forwarded unchanged
  through `FileNavigationUseCase`. `StandardFileSystemRepository`'s implementation calls it with the
  files discovered so far, throttled to roughly once/second via `std::chrono::steady_clock` — a
  caller must not add its own throttling on top. `AdvancedSearchService::requestRecursiveScan` takes
  a matching `onProgress` parameter and bridges each tick back to the caller's thread with the same
  `QPointer<QObject> guard(context)` + `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
  double-hop used for the final result, so a tick that arrives after `context` (the `TabViewModel`)
  is destroyed is silently dropped. This is what lets a slow scan populate the results pane as it
  goes instead of only once the whole subtree has been walked.
* **A second `SQLiteTagRepository` connection.** `SQLiteTagRepository` wraps one `QSqlDatabase`
  connection, and Qt requires a connection to be used only from the thread that opened it — the
  existing single instance (owned by `CompositionRoot`, used from the UI thread) can't simply be
  called from `AdvancedSearchWorker`'s thread too. `AdvancedSearchWorker` instead owns its **own**
  `SQLiteTagRepository` (+ its own `TagManagementUseCase` wrapping it), pointed at the same
  `explorer.db` file, constructed on the worker thread itself (after `moveToThread`, not in a
  constructor that runs on the main thread) rather than injected from `CompositionRoot`.
  `SQLiteTagRepository` now sets `PRAGMA busy_timeout` on open, since a second live connection to the
  same file means a UI-thread tag write and a worker-thread tag read can now genuinely race; without
  a timeout, either connection would fail immediately with `SQLITE_BUSY` instead of retrying briefly.
  `FileNavigationUseCase`/`IFileSystemRepository`, by contrast, needed no such duplication —
  `FilePreviewViewModel`'s worker thread already calls into the same shared instance concurrently
  with the UI thread (§14.26), since plain `std::filesystem` calls carry no shared connection object
  to serialize.
* **`AdvancedSearchService`** (`adapters/viewmodels`) — owns the `QThread` + `AdvancedSearchWorker`,
  owned long-lived by `CompositionRoot` (constructed with `appDataDir / "explorer.db"`, same lifetime
  posture as `GitStatusService`). Exposes `requestRecursiveScan`/`requestTagFirstCandidates(QObject*
  context, ..., callback)` — the `GitStatusService` shape (`QPointer<QObject> guard(context)` around
  a `QMetaObject::invokeMethod` double-hop), not `FilePreviewViewModel`'s own-signals shape, because
  the caller here (`TabViewModel`) is per-tab and can be destroyed (tab closed) while a request is
  still in flight, unlike the single long-lived `FilePreviewViewModel`/`GitStatusService` instances.
* **`TabViewModel`** gains an `int m_advancedSearchGeneration` counter, bumped on every
  `runAdvancedSearch` dispatch and on `exitAdvancedSearch()`. Both the completion callback
  (`onAdvancedSearchCandidatesReady`) and the new progress callback (`onAdvancedSearchProgress`,
  below) check the generation they were dispatched with against the current one before applying a
  result — a stale reply (superseded by a newer edit, or arriving after the panel was
  closed/the tab navigated away) is silently discarded, the same pattern
  `FilePreviewViewModel::onWorkerFinished` already uses. Re-filtering an already-cached
  recursive-scan snapshot (no rescan needed) is unchanged and stays synchronous — it was never the
  source of the freeze, being pure in-memory filtering. A new `advancedSearchLoading()` signal fires
  right before each async dispatch, and a separate `advancedSearchLoadingFinished()` signal fires
  once that dispatch's result actually arrives (as opposed to `advancedSearchResultsChanged`, which
  also fires for every intermediate progress tick) — `SearchCriteriaPanel` uses the loading/
  loadingFinished pair (plus `advancedSearchFailed`) to show a "Searching…" state on its Search
  button for the whole scan, not just until the first partial result. The button stays *enabled*
  throughout: clicking it (or editing a field) while a search is already running is how the user
  restarts it with the latest criteria, per the reuse rule below.
* **Reusing an in-flight scan instead of racing it.** `m_advancedSearchScanPending` tracks whether a
  recursive scan for the current root is currently running. When a criteria edit arrives
  (`updateAdvancedSearchCriteria`/`removeTagSearchCriterion`, `forceRescan=false`) while one is
  pending, `runAdvancedSearch` does **not** dispatch a second concurrent walk of the same subtree —
  it just re-filters `m_advancedSearchPartialSnapshot` (the candidate list from the most recent
  progress tick, empty before the first tick arrives) synchronously against the latest criteria. The
  scan that's already running keeps reporting progress — and eventually its final result — filtered
  against whatever `m_advancedSearchCriteria` holds *at the time each tick fires*, so continuing to
  type is never lost, and no CPU/disk work is duplicated just because the user typed again before
  the previous walk finished. A `forceRescan=true` call (Search clicked, or the first tag added)
  always dispatches fresh regardless, bumping the generation so any older in-flight scan's eventual
  reply is discarded rather than clobbering the new one.
* **Not built:** true cancellation of an in-flight scan/stat-loop — a superseded scan still runs to
  completion in the background even though its result is discarded by generation (same posture as
  §14.26's preview generation: neither `std::filesystem`'s recursive iterator nor a `stat()` loop is
  interruptible mid-call); progress reporting for the tag-first path (cheap enough — one DB query +
  a `stat()` loop — that it wasn't judged worth the complexity); the same async/progressive treatment
  for quick search (§14.18), which keeps its original synchronous-on-the-UI-thread posture for now.

### 14.39 Customizable Details-View Columns (Per-Tab Visibility + New Tag/Date-Created Columns)

Adds two new Details-view columns (Tags, Date created) and a per-tab column-visibility toggle,
right-click-on-header, alongside a Settings page controlling the default for newly created tabs.
Name is excluded from the toggle (always visible, matching Explorer).

* **`FileListModel::Column`** gains `TagsColumn, DateCreatedColumn`, appended before `ColumnCount`
  — no existing column index shifts (the same convention `GitStatusColumn` established). Date
  created reads `FileNode::creationDate()`, already present on the entity — no Domain change.
  Tags renders as colored chip pills (`TagsCellDelegate`, `src/ui/widgets`, installed only on this
  column via `QTreeView::setItemDelegateForColumn`), not plain text, backed by a new
  `Role::TagsRole` returning that row's `std::vector<Tag>`.
* **Bulk tag lookup.** `ITagRepository`/`TagManagementUseCase` gain `tagsForFiles(paths)`/
  `tagsForFiles(files)`, returning a `TagsByPathMap` (`src/application/FileTagsByPath.h`) — the
  first bulk (as opposed to single-path) tag query, needed so listing a directory doesn't run one
  DB query per visible row. `PathHash` (the `std::filesystem::path` hasher needed for any
  `unordered_map<path, ...>`) moved out of `GitDirectoryStatus.h` into its own
  `src/domain/PathHash.h` so this new map doesn't have to include git-status types to get it.
  `SQLiteTagRepository::tagsForFiles` batches its `IN (...)` query (`kTagsForFilesBatchSize = 500`
  paths per query, merged) to stay under SQLite's compiled `SQLITE_MAX_VARIABLE_NUMBER` (999 on many
  builds) regardless of how many files a directory holds.
* **`FileTagsService`/`FileTagsWorker`** (`src/adapters/viewmodels`, new) — runs `tagsForFiles` on
  its own dedicated background `QThread`, off the Qt UI thread, the same "own thread + own lazily-
  opened `SQLiteTagRepository` connection (thread-affine `QSqlDatabase`) + `QPointer<QObject>`-guarded
  callback" shape `GitStatusService`/`AdvancedSearchWorker` already establish — `TabViewModel` no
  longer calls `TagManagementUseCase::tagsForFiles` synchronously on the UI thread.
  `TabViewModel::requestFileTags` dispatches through it from the same `directoryContentsChanged`
  funnel `requestGitStatus` uses, discarding a stale reply for a path that's no longer current the
  same way. `requestSearchResultTags`/`requestAdvancedSearchResultTags` do the same for the quick-
  search and advanced-search result models (§14.18/§14.19) — previously neither ever populated Tags
  at all — caching the fetched map (`m_searchSnapshotTags`/`m_advancedSearchSnapshotTags`) so a
  same-snapshot re-filter (`updateSearchQuery`, every `applyAdvancedSearchFilter` call) reapplies it
  with no new DB round trip; only a changed snapshot/candidate list (a new scan result, a progress
  tick, the tag-first branch's result) triggers a fresh fetch. Owned long-lived by `CompositionRoot`
  like `GitStatusService`/`AdvancedSearchService`.
* **Live refresh via `TagChangeNotifier`** (`adapters/viewmodels`, new) — a single-signal
  (`tagsChanged()`) broadcaster owned by `CompositionRoot`, injected into `TagListViewModel` (emits
  after a successful assign/unassign, §14.9), `TagManagerViewModel` (emits after create/rename/
  delete, §14.17), and `TabViewModel` (re-runs its bulk tags fetch on receipt, for whichever of
  normal browsing/quick search/advanced search is currently active). Deliberately coarse: any tag
  change anywhere re-fetches every open tab's Tags data for its own current directory/search results,
  rather than diffing which paths actually changed — correctness over precision, and still cheap
  (one local query per open tab, off the UI thread).
* **Per-tab visibility, not global.** Unlike column *widths* (one shared
  `AppConfig::detailsColumnWidths` array applied to every tab), column *visibility* is genuinely
  per-tab — it lives directly on each `FileBrowserView`'s `QHeaderView` (`setSectionHidden`/
  `isSectionHidden`), exposed via `FileBrowserView::setColumnVisible`/`isColumnVisible` and a
  `columnVisibilityChanged(Column, bool)` signal. A tab's three `FileBrowserView` instances (normal
  browsing, quick-search results, advanced-search pane, §14.19/§14.18) are kept in sync by that
  signal, the same "seed all three the same way" posture their column widths already have. Right-
  click on `QTreeView::header()` (`Qt::CustomContextMenu`) opens a `QMenu` of checkable per-column
  actions (Name excluded). *Superseded in part by §14.49:* this per-tab choice is now also
  persisted across restarts via `TabConfig.hiddenColumns`, rather than being purely a UI-widget-
  layer, session-only toggle with no ViewModel/Application involvement.
* **Settings > General > Columns** ("Default visibility for new tab" group box, one checkbox row
  per non-Name column) follows the established `NotificationSettings*` shape exactly:
  `ColumnVisibilitySettings` (plain struct) + `ColumnVisibilitySettingsStore` (JSON, tolerant load/
  atomic save) + an `AsyncSaveDispatcher<ColumnVisibilitySettings>` member (background-thread save)
  + `ColumnVisibilitySettingsViewModel` (Qt wrapper, owned once by `CompositionRoot`), persisted to
  its own `column_visibility.json` (§9 gains this file alongside `notification_settings.json`).
* **`AsyncSaveDispatcher<T>`/`AsyncSaveWorker<T>`** (`src/adapters/config`): every `*ViewModel` above
  used to hand-roll an identical "worker `QObject` moved to a dedicated `QThread`, save dispatched via
  a queued functor" skeleton (`FileDecorationRulesSaveWorker`, `HighlightThemeSaveWorker`,
  `FavoritesSaveWorker`, `NotificationSettingsSaveWorker`, `ColumnVisibilitySettingsSaveWorker`),
  differing only in the value type being saved. `AsyncSaveWorker<T>` is a template `QObject` (no
  `Q_OBJECT` — templates can't be moc'd, and none of `QObject`'s meta-object machinery beyond thread
  affinity is needed) wrapping a caller-supplied `std::function<void(const T&)>`; `AsyncSaveDispatcher<T>`
  owns the `QThread` + `AsyncSaveWorker<T>` pair and exposes one `dispatch(T value)` call. Each
  `*ViewModel` now holds one `AsyncSaveDispatcher<ValueType>` member constructed with its store's
  `save()` call bound via a lambda, and calls `dispatch(currentValue)` from its existing
  "mutate → emit changed signal → persist" tail — disk I/O still never runs on the Qt UI thread, with
  no per-type worker class left to write.
  Read live (a `ColumnVisibilityViewModel&` reference, not a value copied at startup like
  `m_initialColumnWidths`) at the moment each new tab's `FileBrowserView`s are constructed, so a
  Settings change takes effect for tabs opened afterward without touching already-open ones.
* **Not built:** sorting by Tags/Date created (no new `SortCriterion`); persisting a specific tab's
  chosen visibility across app restart (only the new-tab *default* is a persisted setting); tag
  chips in this column being clickable/interactive (unlike the tag panel's chips, §14.9/§14.20).

### 14.40 Disk Usage / Folder Size (Bottom Panel)

A second bottom-panel tab (§14.24), "Folder Size", next to "Terminal" in the same
`BottomPanelWidget`: a pie chart of the current folder's largest immediate children (subfolders by
recursive size, files by their own size) plus "Scan Drive"/"Scan Folder" buttons. Deliberately an
isolated vertical slice — it does not touch `FileNode`, `FileListModel`, tagging, Preview, or any
existing use case, aside from reusing `TabViewModel::navigateTo` for segment-click navigation.

* **Domain:** `DiskUsageNode` (`path`, `name`, `size` — recursive aggregate for a directory, own
  byte size for a file — `isDirectory`). Kept separate from `FileNode`, whose `size()` means the
  item's own size only; overloading that meaning for directories would be a footgun for every
  existing `FileNode` caller.
* **Application (new Ports):** `IDiskUsageScanner::scan(root, onProgress)` — walks `root` and every
  descendant, returning a flat `vector<DiskUsageNode>` with each directory's `size` already the
  recursive aggregate (adapter computes it bottom-up during the walk); `onProgress` is a lightweight
  periodic tick only (no partial data — v1 shows "Scanning..." on the button, not incremental
  results). `IDiskUsageRepository::saveScan(scanRoot, nodes)` (batched upsert, one transaction) and
  `::childrenOf(folder)` (cached immediate-children lookup, for redisplaying an already-scanned
  folder after a segment-click navigation without rescanning). `DiskUsageUseCase` (new, stateless):
  `scanAndPersist(root)`, `topChildren(children, max = 10)` (pure — sorts descending by size,
  truncates; each shown segment's percentage is computed against the *full* children total, not the
  sum of the shown top-N), `childrenFor(folder)` (passthrough to the repository).
* **Adapters — `src/adapters/diskusage`** (new module): `WindowsDiskUsageScanner` (Windows-only,
  `Result` failure elsewhere, same posture as `showProperties`) walks via `FindFirstFileW`/
  `FindNextFileW` rather than `std::filesystem::recursive_directory_iterator` — sizes/attributes come
  straight off `WIN32_FIND_DATAW`, no per-entry `stat()` call, which is the actual efficiency win for
  this bulk-aggregation workload. Long paths go through the existing `WindowsLongPath::withPrefix()`
  helper; wide `cFileName` converts straight to `std::filesystem::path`, never narrowed through
  `.string()` (§6, non-ASCII names). Does not recurse into `FILE_ATTRIBUTE_REPARSE_POINT` directories
  (avoids junction/symlink cycles and double-counted sizes). `SQLiteDiskUsageRepository` implements
  `IDiskUsageRepository` against a `filesystem_index` table (schema in §7) over its **own**
  `QSqlDatabase` connection to the same `explorer.db` file — driven from a background thread (below),
  so it needs its own thread-affine connection, same rule as `AdvancedSearchWorker`'s dedicated
  `SQLiteTagRepository` connection.
* **Threading:** `DiskUsageService`/`DiskUsageWorker` (`src/adapters/viewmodels`), the same
  "dedicated `QThread` + lazily-opened own SQLite connection" shape as `AdvancedSearchService`/
  `AdvancedSearchWorker` and `FileTagsService`/`FileTagsWorker`. `requestScan(context, requestId,
  root, onProgress, onFinished)` is queued to the worker thread via `QMetaObject::invokeMethod`;
  both callbacks are bounced back to `context`'s thread the same way, guarded by a `QPointer` so
  neither fires after `context` is destroyed — same shape §14.52's `FragmentationService` later
  established. **Cancellable, same mechanism as §14.52**: `requestId` is caller-supplied (each
  `FolderSizeViewModel` passes the `NotificationCenter` `ProgressId` it creates for its own scan), a
  `QMutex`-guarded `QHash<int, shared_ptr<atomic<bool>>>` holds one real cancel flag per in-flight
  request, and `cancel(requestId)` flips it; `WindowsDiskUsageScanner::walkDirectory` polls it once
  per directory entry (coarse, not interruptible mid-syscall, same granularity
  `WindowsFragmentationScanner` checks at) and a set flag unwinds the whole in-flight recursive walk
  within one loop iteration per level, surfacing as an `ErrorCode::Cancelled` failure (never
  persisted). `onProgress(scanned, currentPath)` fires throttled to ~once/sec during the walk — since
  this scanner has no separate "count candidates first" pass (unlike Fragmentation's two-phase scan),
  it is always the indeterminate case: a running discovered-count and the entry just visited, never a
  known total. Multiple tabs' requests queue on the one worker thread, same posture
  `AdvancedSearchWorker` already accepts. Only one scan in flight per tab; both buttons disable for
  the duration.
* **ViewModel/UI:** `FolderSizeViewModel` (`adapters/viewmodels`, one per tab, same lazy-start
  lifetime as `TerminalWidget`) tracks the tab's current path (read-only via
  `TabViewModel::currentPathChanged`) and drives `DiskUsageService`. It also holds a
  `NotificationCenter&` it calls into directly to create/update/terminate its own cancellable
  progress-bar card per scan — the same "ViewModel calls into NotificationCenter directly" posture
  §14.52's `FragmentationViewModel` established — and forwards a scan failure (other than
  `ErrorCode::Cancelled`) to `FolderSizeWidget` via `scanFailed`, which raises it as an Error popup.
  `PieChartWidget`
  (`src/ui/widgets`, new, plain `QPainter` — no QtCharts/new vcpkg dependency for one chart) paints
  up to 10 wedges with name/percentage/size labels and emits `segmentClicked(path)` on hit-test.
  Labels form a single legend column on the **right** of the pie — one line each, `<color swatch>
  [name]  NN.N%  size`, the name eliding when the column is too narrow — stacked at their wedge's
  rim height, decluttered by the usual two-pass nearest-neighbour push, and spread evenly over the
  panel's height when even minimum spacing won't fit all 11 rows. Leader lines are drawn in a muted
  neutral, never the wedge's color (the swatch already carries that), and always leave the circle
  *horizontally at the right rim* at the wedge's own height — mirrored across for a wedge pointing
  left — so a leader never crosses the pie. Angularly adjacent wedges thinner than 2% of the circle
  share one merged trunk out of the rim and only fan out into per-label branches once clear of it,
  instead of each drawing its own colliding line;
  `FolderSizeWidget` hosts it plus the two buttons (text flips to "Scanning..." and both disable
  while a scan is in flight) and wires `segmentClicked` to `TabViewModel::navigateTo`. Registered via
  a second `bottomPanel->addPanelTab(tr("Folder Size"), folderSizeWidget)` call in
  `WorkspacePaneWidget::addPageForTab`, right after the existing Terminal registration —
  `BottomPanelWidget::addPanelTab`'s panel-tab button `objectName` becomes index-derived (it was
  hardcoded for the single pre-existing Terminal caller) so each panel tab's button keeps a distinct,
  stable id (§11's UI-Automation-id convention).
* **Not built:** Linux scanner (Port exists; adapter returns "not supported"); auto re-scan on
  navigation, live directory-change events, or a timer; persisting the bottom panel's
  expanded/collapsed state (unchanged from §14.24); any integration with tagging/Preview/search.
  (Progress bar + cancellation *were* initially cut here, per the "Not built" line above as it
  originally read — reused §14.52's `NotificationCenter`-driven cancellable progress-card mechanism
  wholesale once Fragmentation established it, with only one bar and always the indeterminate
  "running count + current path" shape, never Fragmentation's two-phase determinate one.)
* **Details-view Size column integration:** `TabViewModel` reuses `DiskUsageService::requestChildren`
  (the same cached-lookup call `FolderSizeViewModel` uses, no scan triggered) from its existing
  `directoryContentsChanged` funnel, the same way it already enriches `FileListModel` with git
  status/tags — dispatched alongside `requestGitStatus`/`requestFileTags`, discarding a stale reply
  the same way (`path != currentPath()`). The result is pushed into a new
  `FileListModel::setFolderSizes(FolderSizesByPathMap)` slot; `data()`'s `SizeColumn`/`SizeBytesRole`
  show a directory row's cached recursive size when present, staying blank (as before) for a folder
  never covered by a scan. Read-only and best-effort: it never triggers a scan itself, and is not
  wired into the quick-search/advanced-search result models (`m_searchResultsModel`/
  `m_advancedSearchResultsModel` stay untouched, matching the "not integrated with ... search" cut
  above).
* **Details-view Percent column:** a further `FileListModel::Column` (`PercentColumn`, appended
  after `DateCreatedColumn` — same no-index-shift convention as every prior column addition) shows
  each row's share of the current folder's total size as a blue-ish bar (filled left-to-right
  proportional to the percentage) with the "NN%" label centered in the bar. Computed entirely
  inside `FileListModel` from data it already holds — no new Port/thread — via a new
  `knownSizeOf(FileNode)` helper (file → own size, directory → `m_folderSizesByPath` lookup or
  unknown, replacing the duplicated logic `SizeColumn`/`SizeBytesRole` had) and a cached
  `m_totalKnownSize` (sum of `knownSizeOf` over every entry in the folder, unknown entries
  contributing 0, recomputed in `setEntries()` and `setFolderSizes()`), exposed to the paint code
  via a new `PercentRole`. Denominator basis mirrors the pie chart's own rule above: the *full*
  children total, not just the visible-after-hidden-filter subset — so, like the Size column, a
  folder with unscanned subfolders won't have its rows' percentages sum to 100%, and an unscanned
  subfolder's own row shows no bar. Painted by a new `PercentCellDelegate`
  (`src/ui/widgets`, same `QStyledItemDelegate`-with-raw-`QPainter` shape as `TagsCellDelegate`,
  installed via `setItemDelegateForColumn` the same way), which falls back to a blank cell when
  `PercentRole` is invalid. No dedicated `SortCriterion`: clicking the header falls through to the
  existing `default: SortCriterion::Name` branch, same as `GitStatusColumn`/`TagsColumn`/
  `DateCreatedColumn` today.

### 14.41 Left Panel with Folder Tree (Tree Tab)

A collapsible, resizable side panel on the window's **left** edge — same mechanics as the right
panel (§14.25): header-strip tab button, starts collapsed, click-to-toggle, drag-to-expand — with a
single "Tree" tab: a folders-only tree rooted at "This PC" that follows the focused pane's active
tab. No Domain/Application change, no new Port, no schema change.

* **`SidePanelWidget` replaces `RightPanelWidget`.** §14.25 kept `RightPanelWidget` separate from
  `BottomPanelWidget` because their *orientation* differs; the left and right panels share
  orientation and collapse axis and differ only in which edge the header strip sits on, so one
  class serves both: `SidePanelWidget(Edge, idPrefix)` puts the header strip on the edge facing the
  workspace (`Edge::Right` → left edge, `Edge::Left` → right edge) and names itself
  `<idPrefix>Widget`/`<idPrefix>Stack`. `addPanelTab(label, content, buttonObjectName)` takes each
  button's `objectName` from the caller (the bottom panel's §14.40 move), so the right panel's
  existing UI-Automation ids are unchanged. `BottomPanelWidget` is untouched.
* **Splitter.** `MainWindow`'s central splitter becomes `[leftPanel, workspace, rightPanel]`
  (workspace non-collapsible). Each panel's `expansionChanged` handler trades width with the
  workspace only; the other side panel keeps its current width. Expanded state/width is not
  persisted (same as §14.25).
* **`FolderTreeModel`** (`adapters/viewmodels`, new): a lazily-populated `QAbstractItemModel` of
  directories only — invisible root → "This PC" (`VirtualPaths::ThisPC`) → its drives/quick-access
  folders (repository order, `FileNode::displayName()` honored) → real subfolders sorted
  case-insensitively (`UnicodeCaseFold`, never `path::string()`). Owned by `WorkspaceController` and
  retargeted to the focused tab alongside `TagListViewModel`/`FilePreviewViewModel` (§14.9/§14.26);
  the UI never constructs it (§14.6). One level is listed per expand via
  `FileNavigationUseCase::listDirectory` (`hasChildren` optimistic until listed, `canFetchMore`/
  `fetchMore` do the listing, a failed listing just makes a leaf). Listing is synchronous on the UI
  thread — the same accepted gap as navigation listings (§5); one level per call keeps it small.
  * `indexForPath` walks from "This PC" down (the most specific This PC entry wins, so a Downloads
    subfolder is revealed under "Downloads"), listing missing levels as it goes and returning the
    deepest node reached. A path inside an archive stops at the archive's containing folder —
    archives are files and never appear; browsing into archives from the tree is out of scope.
  * **Hidden folders** follow the global "show hidden files/folders" toggle (§14.21) — except that
    hidden folders on the path to the last revealed folder are always listed, otherwise the tree
    could never follow a tab browsing under e.g. `AppData`.
  * **Staying current without extra I/O:** the retargeted tab's own `directoryContentsChanged`
    (navigation, refresh, §14.33 live-watch) feeds `applyListing`, and
    `FileOperationsController::directoryContentsMayHaveChanged` drives `refreshDirectory`. Both diff
    by path (insert/remove/move rows) so surviving children keep their loaded/expanded subtrees.
    Folders expanded in the tree but not open in any tab are not live-watched (F5 in the tree
    re-lists the current node).
  * Column 0 lines up with `FileListModel::NameColumn` and answers its `FilePathRole`/
    `IsDirectoryRole`, so `FileNameEditDelegate` (§14.13.4) works unchanged. "This PC", drive roots
    and quick-access folders are not editable.
* **Tabs survive folder rename/delete.** Renaming or deleting from the tree routinely hits an
  ancestor of an open tab's folder (rare from the main view, where this was a latent bug).
  `FileOperationsController` gains `pathRenamed(source, destination)` (from `renamePath`) and
  `pathRemoved(path)` (from `moveToTrash`/`deletePermanently`), both emitted *before* the existing
  `directoryContentsMayHaveChanged` so the tree renames its node before the parent's re-list diff
  runs. `WorkspaceController` moves every tab at/below the path (any pane) — to the rebased path
  (`rebasePath`, new in `PathContainment`) after a rename, to the parent after a delete — via a new
  `TabViewModel::replaceCurrentPath`, which swaps the current history entry in place
  (`NavigationHistory::replaceCurrent`) instead of pushing a new one.
* **`FileItemContextMenu`** (`src/ui/widgets`, new, static helper like `ContextMenuBuilder`): the
  body of `WorkspacePaneWidget::showItemContextMenu` and its confirm-then-delete flow, extracted so
  the tree shows the *same* item menu. Caller-specific parts are callbacks (rename, delete — null
  disables the action — error reporting, and an optional "Open" override). The main view's menu is
  unchanged; in the tree "Open" navigates the focused tab instead of shelling out, drive roots and
  quick-access folders get the menu with Rename/Delete disabled, and "This PC"/empty space get no
  menu.
* **`FolderTreeView`** (`src/ui/widgets`, new, `QTreeView`): single click on a row navigates the
  focused tab (`WorkspacePaneWidget::navigateAndSelect`, keeping the §14.34 monitored-scope guard;
  QTreeView emits no `clicked` for the expand arrow, which only expands/collapses); Enter
  navigates; F2 renames inline; Delete/Shift+Delete delete with confirmation. Every
  `activePathChanged` expands the ancestors and selects the folder programmatically (no navigation
  feedback loop); while the panel is collapsed revealing is skipped and `showEvent` catches up, so a
  collapsed panel costs no directory listing. After a tree click the tree re-reveals the tab's
  actual folder, undoing the selection if navigation failed or was declined.
* **Not built:** files, multi-select or drag-and-drop in the tree; a background ("New Folder") menu
  on empty tree space; async listing. (A second left-panel tab, "Search," is added by §14.42.)
  Persisting the panel's expanded/active-tab/width state across restarts is covered by §14.48.

### 14.42 Search Tab (Left Panel)

Adds a "Search" tab to the left `SidePanelWidget`, alongside "Tree" (§14.41), replacing the former
toolbar quick search (§14.18) with an async, list-or-tree-capable equivalent rendered in the left
panel. **Advanced Search (§14.19/§14.20, async since §14.38) is functionally untouched** — same
dedicated per-tab pane, same Name/Min size/Max size/tag fields and Search button, same in-place
results view, same `TabViewModel` API. The only change made to it here is cosmetic: its
entry-point button relocates from the main toolbar into this new Search tab, below the search bar.
No Domain/Application/Port/schema change.

* **§14.18's quick search becomes async, in place of being removed.** `TabViewModel`'s existing
  quick-search block (`searchResultsModel()`/`searchActive()`/`searchQuery()`,
  `startSearch`/`updateSearchQuery`/`exitSearch`, `searchModeChanged`/`searchResultsChanged`/
  `searchFailed`) is reimplemented on top of `AdvancedSearchService`/`AdvancedSearchWorker`
  (§14.38) — its own independent generation-fenced pipeline (own snapshot/partial-snapshot/
  generation members), entirely separate from `m_advancedSearch*`, dispatching a
  `SearchCriteria{.nameQuery = query}` (name-only; no min/max/tags) through the same
  `requestRecursiveScan` the Advanced Search pane already uses. `startSearch`/`updateSearchQuery`
  collapse into one slot, `updateSearchQuery(const QString&)` — no Enter-press step, since the
  search bar now searches live as the user types; an empty query is a no-op that clears results
  (§14.18's historical behavior, preserved). New `refreshSearch()` slot forces a rescan (backs the
  Refresh toolbar button) and new `searchLoading()`/`searchLoadingFinished()` signals (mirroring
  the advanced-search pair) let the UI show a busy state. No new debounce timer: per-keystroke
  calls stay cheap via the same in-flight-scan-reuse rule §14.38 already established — the first
  edit against a given root triggers the one recursive scan, every edit before it completes
  re-filters the latest progress tick's partial snapshot in memory, and every edit after re-filters
  the full cached snapshot, also in memory.
* **`SearchPanelWidget`** (`src/ui/widgets`, new) — the Search tab's content, registered via
  `m_leftPanelWidget->addPanelTab(tr("Search"), ..., "leftPanelSearchButton")` next to "Tree." Top
  to bottom: an icon toolbar (Refresh/Clear/View-as-tree (checkable)/Collapse-all, the last enabled
  only while View-as-tree is on); the search `QLineEdit`; an "Advanced Search..." `QPushButton`
  (the relocated former toolbar action, unchanged click behavior —
  `tab->showAdvancedSearchPanel()`, still opening the existing in-place pane in the focused pane's
  tab, not in this panel); a `QStackedWidget` of two result views for *this tab's own* quick
  search. List mode reuses `FileBrowserView` in `DisplayMode::AdvancedSearchResults` (§14.19,
  bound to this search's own `FileListModel`, not the Advanced Search pane's) with every column
  but Name hidden via the existing per-view `setColumnVisible` — so `SearchResultDelegate`'s
  highlight painting and the existing `itemActivated(path, isDirectory)` double-click signal are
  inherited for free, satisfying "name only, with the matching part highlighted." Tree mode is
  new: a `QTreeView` over `SearchResultsTreeModel` (`adapters/viewmodels`, new) — a
  `FolderTreeModel`-shaped but much simpler model (no lazy loading/`fetchMore`; rebuilt wholesale
  from the current flat result snapshot on every change) that groups matches into the folder
  hierarchy between the search root and each hit, reusing a second `SearchResultDelegate` instance
  for the same Name-highlight painting.
* **`WorkspacePaneWidget`'s per-tab stack loses only the old `quickSearchResultsView` page** (its
  in-place quick-search results, now rendered in the left panel instead) — the
  `advancedSearchResultsPane` page (`SearchResultsPane`/`SearchCriteriaPanel`, §14.19) is
  untouched, so a tab's stack goes from three pages to two (normal browsing, advanced search),
  not one.
* **Retargeting to the focused tab.** `MainWindow` mediates directly (no new ViewModel class),
  the same posture it already used for the old toolbar search bar via `m_searchTrackedTab`:
  `onFocusedTabChanged` disconnects the previous tab's quick-search signals from
  `SearchPanelWidget` and connects the newly-focused tab's, pushing its current `searchQuery()`/
  results immediately so the panel always reflects whichever tab is focused — preserving §14.18's
  "a tab's search results and query persist, independent of focus." The existing
  `advancedSearchFailed`→status-bar wiring for Advanced Search is untouched.
* **Double-click navigation.** `SearchPanelWidget::resultActivated(path, isDirectory)` (bubbled
  from whichever result view is active) drives a new `MainWindow::onSearchResultActivated`, which
  calls the focused pane's existing `WorkspacePaneWidget::navigateAndSelect` — a directory
  navigates there directly; a file navigates to `path.parent_path()` with `path` as the entry to
  select/highlight. This is the same entry point Favorites (§14.36) already uses, so no new
  navigation logic was needed. (Advanced Search results keep their existing double-click behavior,
  unaffected by this.)
* **"View as tree" is a panel-level UI preference, not per-tab state** — it changes how the
  focused tab's current quick-search results render, not what is searched; not persisted (same
  posture as §14.25/§14.41's un-persisted panel width/expanded state). It has no equivalent in
  Advanced Search, which keeps its single Details-like results view.
* **Not built:** showing non-matching sibling folders for context in tree mode (grouping is purely
  structural, built only from the matched paths themselves); persisting the view-as-tree toggle;
  true cancellation of an in-flight scan (unchanged from §14.38 — still generation-fenced discard,
  not a real abort); any merging of this tab's quick search with Advanced Search's criteria or
  results — the two remain fully independent search mechanisms sharing only the backend service.

### 14.43 Search Tab Follow-Up: Selection Sync, Enter-to-Refresh, Refresh-After-Navigate

Four small UX fixes to §14.42's Search tab, all UI/Adapters glue — no Domain/Application/Port/
schema change.

* **Selecting a result updates the right panel.** `TagListViewModel`/`FilePreviewViewModel::
  resolveTarget()` already resolve purely from `TabViewModel::selectedEntries()`, same as the main
  browse view — the only gap was that nothing ever called `setSelectedEntries()` from the Search
  tab's own result views. `SearchPanelWidget` now bubbles a `selectionChanged(const
  std::vector<FileNode>&)` signal: for list mode, forwarded directly from `m_listResultsView`'s
  existing `FileBrowserView::selectionChanged` (already built from `FileListModel::entryAt()`, no
  new code there); for tree mode, resolved from `QTreeView`'s selection via `SearchResultsTreeModel::
  pathForIndex()` looked up (case-insensitively, `PathContainment.h`) against a `m_lastResults`
  cache `SearchPanelWidget` now keeps alongside `setResults()`. A selected tree node that is a
  structural ancestor rather than one of the matched entries isn't in `m_lastResults` and resolves
  to "no selection," rather than adding a `stat()`-based fallback for that edge case.
  `MainWindow::createSearchPanel()` forwards the signal to
  `m_workspaceController->focusedTab()->setSelectedEntries(...)`, looked up fresh at signal time —
  the same posture already used there for `resultActivated`/`refreshRequested`, so no new
  disconnect/reconnect bookkeeping was needed in `onFocusedTabChanged`. The main browse view's own
  visual selection is left untouched by a search-result selection (and vice versa); whichever view's
  selection changed most recently is what the right panel reflects.
* **Highlighting matched substrings was already built** (§14.19/§14.42's `SearchResultDelegate` +
  `SearchPanelWidget::setQuery()`) in both list and tree result views — verified still rendering
  correctly, no change needed here. Known narrow gap, left as-is: a query containing `*`/`?` is
  glob-matched by `AdvancedSearchService` but highlighted via a literal substring search, so a
  wildcard query's own literal text usually won't appear in, and therefore won't highlight in, its
  matches.
* **Enter forces a refresh.** `QLineEdit::returnPressed` now triggers the same action as the
  Refresh button (below) with the box's current text — additive to, not a replacement for, the
  existing per-keystroke live search, giving users an explicit way to force a fresh disk scan (live
  search only scans once per root; further keystrokes re-filter the cached snapshot, so files
  added/removed on disk afterward need an explicit refresh to show up).
* **Refresh now works after navigating away mid-search.** `TabViewModel::navigateTo()` has always
  unconditionally called `exitSearch()` (clearing `m_searchActive`/`m_searchQuery`/`m_searchRoot`),
  but nothing cleared the Search tab's own `QLineEdit` text on an in-tab navigation (only
  `onFocusedTabChanged`'s focus-change sync did), so the box could keep showing a stale query after
  the tab moved to a different folder — and the old no-arg, `m_searchActive`-gated
  `TabViewModel::refreshSearch()` was then a silent no-op, since search had already been exited.
  `refreshSearch()` now takes the query explicitly (`refreshSearch(const QString& query)`,
  `SearchPanelWidget::refreshRequested` updated to carry it, also reused by Enter above): it sets
  `m_searchQuery` and force-dispatches `runSearch(true)` unconditionally, which already re-roots to
  `currentPath()` on every forced dispatch and flips `m_searchActive` back on once results land —
  so Refresh/Enter now always (re)search wherever the tab is currently browsing, regardless of
  whether a search was still active. `exitSearch()`/`navigateTo()` themselves are unchanged — fully
  exiting search on navigation is still correct; only Refresh/Enter's ability to *restart* it
  changed.
* **Not built:** preserving the exited search's query/results across a navigation (the box simply
  keeps showing stale text until Refresh/Enter or a focus change resets it — same un-persisted
  posture §14.42 already accepts elsewhere); a visual indicator distinguishing "live filtered
  results" from "stale results, folder changed since" beyond what Refresh/Enter now fix.

### 14.44 Quick Search: Match Case / Whole Word / Exact Name / Regex Toggles

Adds four toggle buttons above §14.42's Search-tab search bar that narrow how the query text is
interpreted. Scoped entirely to the quick Search tab — Advanced Search (§14.19/§14.20) is untouched,
by construction (see API shape below). No Port/schema change.

* **`NameSearchOptions`** (`src/application/NameSearchOptions.h`, new) — a plain struct, `{
  NameMatchMode mode = Contains; bool caseSensitive = false; }`, `NameMatchMode` being `Contains`
  (today's wildcard/substring behavior) / `WholeWord` / `ExactName` / `Regex`. `caseSensitive` is
  orthogonal to `mode` and applies to all four.
* **`FileNavigationUseCase::filterByName`** gains an options-aware overload; the original
  `filterByName(files, query)` becomes a one-line forward to the new overload with default-constructed
  `NameSearchOptions` — so `filterByCriteria`/Advanced Search's call site is untouched and keeps
  exactly today's behavior.
* **Three new domain-level matchers**, mirroring `GlobMatch`'s existing posture (UTF-8 in, matched
  via decoded Unicode codepoints):
  - `GlobMatch::matches` itself gained a `bool caseSensitive = false` parameter (default preserves
    every existing call site, including `FileNamePattern`'s decoration-rule usage) — when true, the
    internal matcher skips `UnicodeCaseFold::foldCodepoint` on both sides.
  - **`WholeWordMatch`** (new) — scans for every occurrence of the query in the name and accepts one
    only if the codepoint immediately before/after (if any) is not a letter/digit. Word-char
    classification is a heuristic (ASCII letter/digit, or any codepoint above `0x7F`), same
    "good enough for real-world names, not a full Unicode table" trade-off `UnicodeCaseFold`
    already documents for itself — so `File` matches `File Name`/`File-Name`/`File_Name`/`File.txt`
    but not `FileName`.
  - **`RegexMatch`** (new) — wraps `std::wregex`. Converts both the pattern and the candidate name
    from UTF-8 to a wide string via `std::filesystem::path(...).wstring()` (reusing the project's
    existing portable UTF-8↔native-wide conversion rather than a hand-rolled one) — giving UTF-16
    matching on Windows and full UTF-32 codepoint matching on Linux "for free." Returns
    `Result<bool>`, not a thrown exception: an invalid live-typed pattern throws `std::regex_error`
    from `std::wregex`'s constructor, caught at this boundary and translated to `Result::failure`
    (the same adapters-catch-third-party-exceptions posture Architecture.md already allows, applied
    here because the pattern is user search-box input, not a programmer constant). Known trade-off:
    on Windows, a codepoint outside the BMP is a UTF-16 surrogate pair, which a `.`/character-class
    regex construct may not treat as a single "character" — acceptable for a search box, not claimed
    as fully Unicode-correct.
  - `ExactName` mode needs no new domain file — it's `name == query` (case-sensitive) or
    `UnicodeCaseFold::foldUtf8(name) == UnicodeCaseFold::foldUtf8(query)` (default), inline in
    `FileNavigationUseCase::filterByName`.
  - An invalid regex pattern makes `filterByName` return zero matches (same as any other
    non-matching query) rather than surfacing a separate validity/error indicator in the UI — a
    deliberate v1 scope cut, not an oversight.
* **`TabViewModel`** gains `m_searchMatchMode`/`m_searchCaseSensitive` (defaults `Contains`/`false`)
  alongside the existing `m_searchQuery`, a `searchOptions()` getter, and a `setSearchOptions(const
  NameSearchOptions&)` slot. Toggling a mode/case flag **never** dispatches a rescan — it only
  re-filters whatever candidate list (`m_searchSnapshot` or `m_searchPartialSnapshot`) is already
  cached/in-flight, the same cheap synchronous path `updateSearchQuery` already takes once a scan of
  the current root exists. `exitSearch()` resets both back to their defaults, same as it already does
  for `m_searchQuery`.
* **`SearchPanelWidget`** adds four checkable text-label `QToolButton`s to the existing icon toolbar
  row (`Aa` / `ab` / `=` / `.*`, full tooltips with the exact examples from this spec), separated from
  the Refresh/Clear/View-as-tree/Collapse-all group by a `QFrame::VLine` — consistent with the
  project having no custom icon/SVG assets anywhere (every existing button icon is a `QStyle::SP_*`
  standard icon) and reusing the text-only `QToolButton` pattern "Collapse All" already established.
  Whole Word/Exact Name/Regex sit in one `QButtonGroup` with
  `setExclusionPolicy(QButtonGroup::ExclusionPolicy::ExclusiveOptional)` — Qt6's exclusive-but-
  zero-allowed mode, letting a click on the already-active button deselect it back to Contains mode,
  which a classic exclusive `QButtonGroup`/radio-button group cannot do. Match Case stays outside that
  group. A new `searchOptionsChanged(const NameSearchOptions&)` signal and `setOptions(...)` method
  (the latter `QSignalBlocker`-guarded, mirroring `setQuery()`) round-trip through `MainWindow` the
  same way query/results already do, including on `onFocusedTabChanged` retargeting — so each tab's
  toggle state, like its query, is restored independently when that tab regains focus. Not persisted
  across app restarts, same posture as the un-persisted View-as-tree toggle.
* **Highlighting** (`SearchResultDelegate`/`FileBrowserView`) now takes a case-sensitivity flag
  alongside the existing highlight query, so Match Case also governs which part of a result's name is
  bolded — correct for Contains/Whole Word/Exact Name, all three being, once matched, a literal
  substring of the name. **Not built:** pixel-accurate highlighting of the actual matched span in
  Regex mode — the literal-substring highlight attempt is reused as-is, same precedent as this file's
  already-accepted gap for wildcard-pattern highlighting in Contains mode (§14.43).
* **Not built:** any equivalent of these four toggles on the Advanced Search pane's Name field;
  persisting toggle state across restarts.

### 14.45 Quick Search: Skip Git-Ignored Files/Folders Toggle

Adds a fifth Search-tab toggle, next to §14.44's four: when enabled, a quick search run inside a git
working tree never scans into (or returns results from) anything `.gitignore` would exclude. Unlike
§14.44's toggles, this one changes what gets *scanned*, not just what's shown — the point is to avoid
walking large ignored trees (`node_modules`, `build`, ...) at all, so toggling it forces a rescan
rather than re-filtering the cached snapshot.

* **`NameSearchOptions` gains `bool skipGitIgnored = false`** — orthogonal to `mode`/`caseSensitive`,
  same posture as `caseSensitive` itself.
* **New Port method, `IGitStatusProvider::ignoredPaths(root) -> Result<vector<path>>`** — every
  ignored path under `root` (files and *wholly*-ignored directories each as one entry, matching plain
  `git status`'s own default of not descending into an already-ignored directory), empty (not an
  error) when `root` isn't inside a repository, same tolerate-by-blanking posture as
  `statusForDirectory` (§14.32). `GitStatusUseCase` gets a thin passthrough.
  `LibGit2Repository::ignoredPaths(relativeDirectory)` (`src/adapters/git`) is a new sibling to
  `statusForImmediateChildren`: its own `git_status_list_new` call, pathspec = `relativeDirectory`
  (recursively, no immediate-children-only rollup needed here, unlike that method),
  `GIT_STATUS_OPT_INCLUDE_IGNORED` set and `RECURSE_IGNORED_DIRS` left off — the same mechanism
  `statusForImmediateChildren`'s doc comment already credits for untracked folders, symmetric for
  ignored ones, filtered to `GIT_STATUS_IGNORED` entries and returned as absolute paths.
  `LibGit2StatusProvider::ignoredPaths` opens a fresh repo per call (no caching, matching every other
  method on that class) and translates "no repository" into `Ok({})`.
* **`IFileSystemRepository::listDirectoryRecursive` gains a required third parameter**,
  `const std::function<bool(const std::filesystem::path&)>& shouldPrune` (a plain required param on
  the Port's virtual method — a default argument on a pure-virtual signature is a known footgun with
  mocks/overrides; convenience defaults live only on `FileNavigationUseCase`'s non-virtual wrapper,
  `listDirectoryRecursive(root, onProgress = nullptr, shouldPrune = nullptr)`, so the method's one
  existing caller is unaffected). `StandardFileSystemRepository`'s implementation switches from a
  range-based `for` over `fs::recursive_directory_iterator` to an explicit-iterator loop so
  `it.disable_recursion_pending()` is reachable: an entry for which `shouldPrune` returns true is
  both excluded from the result and, if it's a directory, never descended into — the actual
  I/O-avoidance payoff, not just a post-hoc result filter. This is the first pruning hook this walk
  has ever had (previously unconditional, not even special-casing `.git`).
* **`AdvancedSearchWorker::scanRecursive` gains a trailing `bool skipGitIgnored = false`** (default
  preserves Advanced Search's own untouched call site — no equivalent toggle exists there, same cut
  §14.44 already made). When true, it opens its **own**, worker-thread-owned
  `LibGit2StatusProvider`/`GitStatusUseCase` pair — never the shared instance
  `GitStatusService`/`GitStatusWorker` use — lazily constructed on first use after `moveToThread`,
  the same precedent §14.38 already established for `AdvancedSearchWorker`'s own second
  `SQLiteTagRepository` connection: a thread-affine resource needs its own instance per thread rather
  than cross-thread sharing. This is a refinement of, not a contradiction of, §14.32's "every libgit2
  call funnels through one worker thread" note — the actual libgit2 constraint is
  `git_repository*` object thread-affinity (never touch the same handle from two threads at once),
  not a process-wide single-thread rule, so a second, wholly independent handle on a second thread is
  safe. It calls `ignoredPaths(root)` (tolerating failure/empty by scanning unrestricted), also folds
  in `workdirPath() / ".git"` itself (git's own status machinery never reports `.git` as "ignored"
  since it isn't part of the working tree at all, but a user skipping ignored content clearly doesn't
  want `.git`'s internals in results either), and builds a
  `std::unordered_set<std::filesystem::path, PathHash>` (`src/domain/PathHash.h`, the same hasher
  `GitDirectoryStatus`'s map already uses) backing the `shouldPrune` predicate passed to
  `FileNavigationUseCase::listDirectoryRecursive`. `AdvancedSearchService::requestRecursiveScan`
  threads the new bool through unchanged otherwise.
* **`TabViewModel::setSearchOptions` forces a rescan specifically when `skipGitIgnored` changes**,
  unlike mode/case changes which keep re-filtering the cached snapshot for free — a prior scan
  without pruning active never excluded anything, and a prior pruned scan never visited the excluded
  paths at all, so neither snapshot can answer the other state from memory. `exitSearch()` resets the
  flag to `false` alongside the existing mode/case reset.
* **`SearchPanelWidget`** gains a fifth text-only, independently-checkable `QToolButton` after the
  existing `Aa`/`ab`/`=`/`.*` group (not part of their exclusive-mode `QButtonGroup`, same
  independent-toggle posture as Match Case), wired into the existing
  `emitSearchOptionsChanged`/`setOptions`/`clearForNoTab` round-trip.
* **Not built:** an equivalent toggle on the Advanced Search pane (§14.19/§14.20), same cut §14.44
  already made; persisting the toggle across restarts; special handling for multiple/nested
  repositories under one search root beyond whatever `git_status_list_new` itself does. No automated
  coverage for the `TabViewModel`/`SearchPanelWidget`/`AdvancedSearchWorker` glue, per the existing
  §11 UI/ViewModel-glue carve-out; `LibGit2Repository::ignoredPaths`/`LibGit2StatusProvider::
  ignoredPaths` get a real-temp-repo adapter test (mirroring `LibGit2StatusProviderTest.cpp`'s
  existing fixtures), `GitStatusUseCase`'s passthrough gets an ordinary mocked-Port test, and
  `StandardFileSystemRepository`'s new pruning behavior gets an adapter test asserting a pruned
  directory's contents never appear in the walk's result.

### 14.46 Search Tab: Inline Advanced Search Toggle, Debounced Quick Search

Two small UX fixes to §14.42's Search tab, both UI-layer only — no Domain/Application/Port/schema
change, `TabViewModel`/`AdvancedSearchService`/`AdvancedSearchWorker` untouched.

* **Advanced Search entry point moves beside the search bar.** `SearchPanelWidget`'s
  `m_advancedSearchButton` ("Advanced Search...", relocated from the main toolbar by §14.42) sat in
  its own full-width row below the search `QLineEdit`. It now shares one `QHBoxLayout` row with the
  search bar instead — the line edit takes the stretch, the button keeps its natural size next to
  it. Purely cosmetic: object name, automation id, styling, and the `advancedSearchRequested()`
  click behavior are all unchanged.
* **Quick search gains a 500ms debounce, superseding §14.42's "No new debounce timer" call.** That
  decision assumed `TabViewModel::applySearchFilter`'s `filterByName`+`sortBy` pass is cheap enough
  to run on every keystroke because it's pure in-memory work with no I/O. That holds for a modest
  result set, but not for a large directory tree: the recursive scan's progress callback
  (`AdvancedSearchWorker`/`StandardFileSystemRepository::listDirectoryRecursive`, throttled to
  ~1/sec per §14.37) delivers increasingly large partial snapshots over the life of a scan, and
  `applySearchFilter` re-filters and re-sorts the *entire* candidate list from scratch — on every
  progress tick *and* on every keystroke — synchronously on the UI thread. For tens/hundreds of
  thousands of candidates this is no longer cheap, and firing it on every single keystroke while
  the user is still typing is what produced the reported lag.
  - `SearchPanelWidget` gains a single-shot `QTimer` (`kSearchDebounceMs = 500`), following the
    exact convention `AddressBarWidget`'s own (120ms, unrelated-purpose) debounce timer already
    established in this codebase: `textEdited` restarts the timer instead of emitting
    `queryChanged` directly; only the timer's `timeout()` emits it, carrying whatever text is in
    the box at that point. Clearing the box (typed to empty, or the Clear button) bypasses the
    delay and emits immediately, same as `AddressBarWidget::onTextEdited`'s empty-input fast path.
    `returnPressed` (Refresh) and a focus-change `setQuery()` resync both stop any pending timer —
    the former so it doesn't redundantly re-fire right after an explicit forced refresh, the latter
    so a timer left running from the *previous* focused tab's typing can't leak a stale query onto
    the newly-focused tab.
  - The live highlight-query update on the result views (`setNameHighlightQuery`/
    `setHighlightQuery`) stays un-debounced — it's a cheap, visual-only, no-scan/no-filter operation,
    so the typed text still highlights instantly even while the actual re-search is still settling.
  - The scan dispatch itself (`AdvancedSearchService::requestRecursiveScan`) was already fully
    asynchronous before this change (posts to `AdvancedSearchWorker`'s thread via a non-blocking
    `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` and returns immediately, including for
    the very first scan of a root) — the debounce's job is purely to coalesce rapid keystrokes
    into fewer `applySearchFilter` calls, not to make the dispatch itself non-blocking (it already
    was).
* **Not built:** moving `applySearchFilter`'s per-progress-tick re-filter/re-sort off the UI thread
  — the debounce reduces how often it runs on keystrokes, but a single very large snapshot's
  periodic progress-tick filtering can still cost a noticeable, if brief, UI-thread stall
  independent of typing cadence; an equivalent debounce on the Advanced Search pane's live
  field-editing (`SearchCriteriaPanel::criteriaEdited`), which has a similar per-keystroke
  synchronous-refilter shape but wasn't reported as laggy.

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

### 14.48 Left/Right Panel State Persistence

Closes the gap §14.25/§14.26/§14.41 explicitly left open: the left (Tree/Search) and right (Tags/
Preview/Monitoring) `SidePanelWidget` instances always start collapsed on launch today, regardless
of how the user left them. Extends `AppConfig`/`AppConfigStore`'s existing session-persistence
mechanism (§14.14, `config.json`, save-on-close only) with one `SidePanelState{expanded,
activeTabIndex, width}` per side panel, rather than any Domain/Application/Port/schema change —
this is UI/adapters-config state only.

* `SidePanelWidget` gains `activateTab(int)` (a public wrapper around the existing private
  click-handling logic, so a config-driven restore goes through the exact same expand/select/
  lazy-activate path a real click does) and `setPreferredExpandedWidth(int)` (overrides the
  otherwise-fixed `collapsedWidth() + 260` default), plus an `activeTabIndex()` getter alongside
  the existing `isExpanded()`.
* `MainWindow` restores both panels once in its constructor (right after `createWorkspace()`'s
  existing `expansionChanged` wiring, so the restore's `activateTab` call drives the same splitter-
  resize lambda a real click would) and captures both panels' state in `closeEvent` (reading
  `centralWidget()`'s `QSplitter::sizes()` for the live width, alongside
  `isExpanded()`/`activeTabIndex()`).
* **Width `0` is the "unset" sentinel**, same convention as `detailsColumnWidths` (§14.14): a panel
  collapsed at close time has no meaningful "last expanded width" to preserve, so it's left unset
  and falls back to `SidePanelWidget`'s own computed default whenever it's next opened.
* **Not built:** the bottom/Terminal panel (`BottomPanelWidget`, §14.24) — out of scope, a separate,
  differently-oriented widget; per-tab/per-pane side-panel state (the side panels are window-global,
  matching their existing window-global construction); live autosave while running (save-on-close
  only, matching every other piece of §14.14's session state).
* **Bugfix uncovered by this persistence work:** `SidePanelWidget::resizeEvent`'s pre-existing
  "dragging the splitter handle out past `collapsedWidth()` auto-reveals the panel" convenience
  (§14.25/§14.41) fired on *any* resize that pushed a collapsed panel's width past that threshold —
  including one caused by the whole window being resized/maximized (`QSplitter` proportionally
  redistributes space across its children on a parent resize too, not just on an explicit handle
  drag), not only an actual handle drag. Harmless before this feature (expand state was never
  saved, so a spurious reveal reset on next launch regardless); once expand state started
  surviving restarts, that same spurious reveal got *persisted* — collapsing both panels and then
  maximizing the window before exiting would silently re-expand them, which then came back open on
  every subsequent launch. Fixed by tracking the owning `QSplitter`'s own total width across
  resizes (`SidePanelWidget::m_lastKnownSplitterWidth`) and only treating the growth as a deliberate
  drag-reveal when that total is unchanged — i.e. the resize only redistributed space between the
  splitter's children rather than the splitter itself growing. Covered by
  `ui_tests/tests/test_side_panel_persistence.py::test_collapsed_panels_stay_collapsed_after_maximizing_before_close`.

### 14.49 Per-Tab Column Visibility Persistence

Reverses the "not added to `TabConfig`/`AppConfig`" half of §14.39's "Per-tab visibility, not
global" decision: the header right-click hide/show toggle (`FileBrowserView::setColumnVisible`,
§14.39) now survives a restart, per-tab, rather than resetting to whatever Settings → General →
Columns' "default visibility for new tab" (`ColumnVisibilitySettingsViewModel`, §14.39) says every
time. Deliberately kept per-tab rather than folded into the global `detailsColumnWidths`-style
`AppConfig` mechanism (§14.14) — unlike width, two tabs showing different columns at once is the
whole point of this feature existing, so a global override would defeat it.

* `TabConfig` (`src/adapters/viewmodels/WorkspaceConfig.h`) gains
  `std::array<std::optional<bool>, FileListModel::ColumnCount> hiddenColumns` — tri-state per
  column: `nullopt` ("never explicitly toggled in this tab") defers to the Settings dialog's
  default exactly as before this feature; `Some(bool)` is this tab's own explicit choice, which
  always wins over that default once set, the same "no second-guessing a restored value" posture
  `viewMode`/`sortCriterion` already have. `NameColumn`'s slot is always `nullopt` — Name can
  never be hidden.
* `TabViewModel` carries it as plain storage (`hiddenColumns()`/`setHiddenColumns(...)`/
  `setColumnHidden(column, bool)`), the same shape `m_viewMode` already has — no new signal, since
  nothing needs to react live to it, only read it back at capture time.
  `WorkspaceController::restoreFromConfig`/`captureConfig` thread it through exactly like
  `viewMode`/`sortCriterion` already are.
* `WorkspacePaneWidget::addPageForTab` applies `tab->hiddenColumns()` as an override *after* the
  existing `applyDefaultColumnVisibility` call (unchanged, still the baseline for any untouched
  column), and a new connection from each view's `columnVisibilityChanged` keeps `tab`'s copy live
  as the user toggles columns, so `captureConfig()` always reads the current truth without any
  separate close-time re-read step.
* JSON (`AppConfigStore`'s existing `tabConfigToJson`/`tabConfigFromJson`): one `QJsonArray` entry
  per column, `null` for `nullopt`, else the plain bool — tolerant of a missing/wrong-length/
  wrong-type array exactly like every other per-field fallback in this file.
* **Not built:** any change to `ColumnVisibilitySettingsStore`/the Settings dialog default itself;
  cross-tab/cross-pane visibility syncing (deliberately per-tab, per §14.39); any Domain/
  Application/Port/schema change.

### 14.50 Light/Dark Theme

Formalizes the app's existing look as a "Light" theme and adds a "Dark" counterpart, selectable
from Settings → General → UI and applied immediately, persisted across launches — pure UI/Adapters
state (one new `QSettings` key), no Domain/Application/Port/schema change.

* The whole app runs under Qt's **Fusion** style (`UiTheme::installFusionStyle()`, set once at
  startup in `main.cpp`, never re-set on a theme toggle) rather than the native Windows style:
  native style mostly ignores `QPalette` for menus/scrollbars/combo popups, so it can't go fully
  dark. The cost is that Light's native controls (buttons/scrollbars/menus) render in Fusion's
  flatter look instead of Windows-native bevels — colors are unchanged, only that chrome style.
* `UiTheme` (`src/ui/widgets/UiTheme.h/.cpp`): `Theme{Light, Dark}`, `currentTheme()` (lazily reads
  `QSettings` key `UI/Theme`, `"Light"`/`"Dark"`, default `"Light"` — same mechanism/key-namespace
  precedent as `WorkspacePaneWidget::kShowTabCloseButtonsSettingsKey`), `paletteFor(Theme)` (Light
  returns Fusion's own stock light palette; Dark is a hand-tuned dark `QPalette`, `QPalette::
  Highlight` kept close to the app's own accent-blue hue rather than Fusion's default blue), and
  `setTheme(Theme, UiThemeChangeNotifier&)` (persists, calls `qApp->setPalette(...)`, then emits
  the notifier's `themeChanged()`).
* `UiThemeChangeNotifier` (`src/ui/widgets/UiThemeChangeNotifier.h/.cpp`): a plain `QObject` with
  one `themeChanged()` signal, same shape and one-shared-instance-owned-by-`CompositionRoot`
  posture as `TagChangeNotifier`. Every widget whose own stylesheet embeds a `UiColors::current()`
  value takes a `UiThemeChangeNotifier&` in its constructor, connects `themeChanged()` to a private
  `applyThemeColors()` method it already needs to call once from its own constructor, and is
  threaded the shared instance down from `CompositionRoot`/`main.cpp` through `MainWindow` ->
  `WorkspaceLayoutWidget`/`SidePanelWidget`/`TagPanelWidget`/`SearchPanelWidget`/
  `MonitoringPanelWidget` -> `WorkspacePaneWidget` -> `BottomPanelWidget`/`SearchResultsPane` ->
  `SearchCriteriaPanel`, and into `SettingsDialog` for the combo box itself.
* `UiColors.h` (previously a flat set of `constexpr const char*` light-mode literals) is now a
  `UiColorSet` struct of the same members, two `constexpr` instances (`kLightColors` — today's
  exact unchanged hex values — and `kDarkColors` — muted-but-legible tints against the dark
  `QPalette`'s backgrounds, hand-tuned rather than inverting hue), and an inline `current()`
  returning whichever is active. Every call site switched from `UiColors::kFoo` to
  `UiColors::current().foo`.
* `MonitoringLogModel` (not a widget) already computes its log-row colors live from
  `UiColors::current()` on every `data()` call rather than caching a `QColor`, so flipping the
  theme only needs the already-fetched rows repainted: it takes the notifier and emits
  `dataChanged(..., {Qt::ForegroundRole})` across the whole model on `themeChanged()`.
* `ToastNotificationWidget`/`ProgressNotificationWidget`'s card chrome (background/border, and the
  latter's secondary labels) switched from literal `#FFFFFF`/`#A0A0A0`/`#707070` to
  `palette(base)`/`palette(mid)` QSS roles instead — these two cards are always short-lived
  (auto-dismissed or user-closed/worker-terminated), so a QSS palette role (which Qt re-evaluates
  against the widget's current `QPalette` at paint time, no reconnection needed) was simpler than
  threading the notifier into them too. The toast header strip's own saturated accent colors
  (`UiColors::current().notification*`) are unaffected, still a non-goal to re-theme (see below).
* Settings → General → UI (`SettingsDialog::buildGeneralUiPage`) gains a `QComboBox` ("Light"/
  "Dark") directly below the existing "Show close button on tabs" checkbox, initialized from
  `UiTheme::currentTheme()`; on change it calls `UiTheme::setTheme(...)` directly. No new signal
  round-trips through `MainWindow` the way `showTabCloseButtonsChanged` does — `setTheme()` itself
  broadcasts via the shared notifier, which reaches every subscribed widget directly, including
  ones `MainWindow` has no handle to (e.g. a dynamically-created `SearchCriteriaPanel` deep inside
  a pane's advanced-search tab).
* `CompositionRoot` owns the shared `UiThemeChangeNotifier`; `main.cpp` calls
  `UiTheme::installFusionStyle()` right after constructing `QApplication`, then
  `UiTheme::setTheme(UiTheme::currentTheme(), ...)` right after constructing `CompositionRoot` —
  before `MainWindow` is built — so the very first paint already has the right palette, no flash
  of the wrong theme.
* **Not built:** a "follow Windows system theme" option (just an explicit Light/Dark choice, per
  what was asked — easy to add a third "System" option later without reshaping the mechanism
  above); re-theming `TagColorPalette` (tag chip colors), `HighlightTheme` (syntax highlighting),
  or `FileDecorationRules` (per-pattern font/colors) — these already have their own
  user-configurable colors unrelated to app chrome, out of scope, not guaranteed legible against a
  dark background; restructuring Git status badge colors or the Monitoring/Toast/close-button
  named semantic accents beyond re-tuning their existing hex values for dark-background contrast.
  Covered by `ui_tests/tests/test_theme_setting.py` (switches to Dark, asserts it round-trips
  after a restart, switches back) — UI is otherwise outside CTest's scope per §11.

### 14.51 Duplicate File Finder

`Tools > Find Duplicate Files...` opens a non-modal `DuplicateFinderDialog` that recursively scans a
chosen folder or drive for files with byte-identical content and lets the user delete the redundant
copies. Deliberately an isolated vertical slice, same posture as Disk Usage (§14.40) — it does not
touch `FileNode`, `FileListModel`, tagging, Preview, or any existing use case beyond reusing
`IFileSystemRepository::listDirectoryRecursive` and the existing trash-delete path.

* **Not the existing identity hash.** `FileNode`'s identity hash (§8) mixes `mtime` into
  `size + first/last 64KB`, which is exactly wrong for content-equality: two files with identical
  bytes but different modification times would hash differently, and two different-content files
  that happen to share size/mtime/edge-bytes would hash the same. This feature computes its own
  full-content hash (no `mtime`, no truncation to 64KB) instead, kept entirely local to the new
  `adapters/duplicates` module rather than changing `StandardFileSystemRepository`'s existing
  `hash()` or its callers.
* **No persistence, unlike Disk Usage.** `IDiskUsageScanner`'s results get cached in
  `filesystem_index` (§7) because redisplaying a stale size is harmless. A stale duplicate group is
  not harmless — it feeds a delete action — so this feature has no repository/SQLite involvement at
  all and always re-scans the live filesystem from scratch.
* **Domain/Application:** `DuplicateFileGroup` (`src/application`, plain struct — Application-layer
  like `GitFileStatus`/`DriveSpaceInfo`, not Domain, since it's a derived result rather than a
  fundamental per-file entity): `{ contentHash, fileSize, paths }`. `IDuplicateFileScanner::scan(root,
  minFileSizeBytes, onProgress, cancelRequested)` (new Port, mirrors `IDiskUsageScanner`, but with a
  real per-file progress callback and a real `std::atomic<bool>` cancel flag rather than Disk Usage's
  "no progress bar, no cancel" posture — a full-content hash pass is slow enough to need both).
  `DuplicateFinderUseCase` (new, stateless, depends only on `IDuplicateFileScanner`, same "never call
  another use case" posture as `FilePreviewUseCase`): `scan(...)` passthrough, plus pure
  `totalWastedBytes(groups)`/`groupsSortedByWaste(groups)`.
* **`src/adapters/duplicates`** (new module, `explorer_adapters_duplicates`, links only
  `explorer_application` + `Qt6::Core` — no `Qt6::Sql`, unlike `adapters/diskusage`, since there is
  no repository class here): `StandardFileHasher` (free function, streamed xxHash64 over a file's
  full content, long-path-safe via the existing `WindowsLongPath::withPrefix()` helper).
  `StandardDuplicateFileScanner` implements `IDuplicateFileScanner`, portable (no OS-specific API,
  unlike `WindowsDiskUsageScanner`): enumerate via the existing
  `IFileSystemRepository::listDirectoryRecursive` → bucket by exact size (drop singleton buckets
  before any hashing) → full-content hash each remaining candidate, checking `cancelRequested`
  between files (file-boundary granularity — same accepted "not interruptible mid-call" posture as
  `WindowsDiskUsageScanner`'s walk, §5) → sub-bucket by `(size, hash)` → **byte-for-byte verification**
  of every remaining sub-bucket (buffered compare against its first file, early-exit on the first
  differing byte), splitting off a mismatch into its own confirmed group instead of dropping it — the
  one extra safety step this feature takes beyond the identity hash's accepted-collision posture
  (§8), because this output drives deletion. Returns every confirmed group of size >= 2.
* **Threading:** `DuplicateFinderService`/`DuplicateFinderWorker` (`adapters/viewmodels`, new), same
  dedicated-`QThread`-owned-worker shape as `AdvancedSearchService`/`DiskUsageService`/
  `FileTransferService`. Cancellation is a real `std::atomic<bool>` per request (like
  `FileTransferService`, not Advanced Search's "discard a stale result") — a whole-drive scan can run
  long enough that Cancel needs to actually stop it. Only one scan in flight; both Scan buttons
  disable for its duration, same as `FolderSizeWidget`.
* **UI:** `DuplicateFinderDialog` (`src/ui/widgets`, new, non-modal — shown via `show()`, not
  `exec()`, so a long scan never blocks browsing in the main window), opened from the previously-empty
  `Tools` menu. Root path field + "Browse..." (pre-filled once from the focused tab's current path at
  the moment the dialog opens — a one-time snapshot, same "fixed at first use" posture as the
  Terminal's cwd, §14.24) plus a minimum-file-size spinbox (default 4 KB, skips trivial/empty files)
  and two buttons, `Scan Folder`/`Scan Drive`, mirroring `FolderSizeWidget`'s existing two-button
  convention exactly (§14.40). Progress reuses the existing cancellable `NotificationCenter`
  progress-bar mechanism as-is (§14.37/§14.47) rather than a bespoke indicator. Results render in
  `DuplicateGroupsModel` (new `QAbstractItemModel` — not a reused `FileListModel`, since
  grouped/checkable rows don't fit its per-folder-listing shape) inside a plain `QTreeView`: one
  top-level row per group with its wasted-space total, expandable to per-file checkbox rows.
  "Check All but Newest/Oldest in Each Group" buttons cover the common keep-one-delete-rest workflow.
  "Delete Checked" confirms (count + bytes freed) then routes through the existing
  `IFileSystemRepository`-backed trash delete — no permanent-delete option in this dialog, so an
  accidental miscall stays recoverable; deleted rows disappear live, and a group that drops to one
  remaining member collapses out of the results. Right-click a row for "Open Containing Folder"
  (reuses existing navigation, same convenience as a search-result double-click, §14.42).
* **Not built:** persistence/caching of results (see above); multi-root/whole-PC scanning (one root
  per scan); a "Move to..." action (trash delete only); any Linux-specific carve-out (this feature has
  no OS-specific code of its own, unlike `WindowsDiskUsageScanner`). No CTest coverage for the dialog/
  model/service beyond §11's existing UI/ViewModel-glue carve-out; `StandardFileHasher` and
  `StandardDuplicateFileScanner` (real temp-directory fixtures, including a same-size-different-
  content case that exercises the byte-for-byte verification step) get ordinary GTest coverage under
  `tests/adapters`, and `DuplicateFinderUseCase`'s pure helpers get mocked-Port coverage under
  `tests/application`.

### 14.52 Disk Fragmentation Viewer

A third bottom-panel tab, "Fragmentation" (§14.24's `BottomPanelWidget`), right after "Folder Size"
(§14.40) — a read-only, per-tab viewer of NTFS file fragmentation, not a defragmentation tool.
Deliberately an isolated vertical slice, same posture as Disk Usage/Duplicate Finder — no
`FileNode`/`FileListModel`/tagging/Preview change, reusing only
`IFileSystemRepository::listDirectoryRecursive` for enumeration and `navigateAndSelect` for one
context-menu convenience.

* **Viewing only, by design.** The product ask is to see fragmentation state, not fix it. Actually
  moving extents (`FSCTL_MOVE_FILE`) is a materially larger, riskier API surface that interacts with
  the OS's own "Optimize Drives" scheduler — explicitly out of scope; a user who wants to defragment
  uses that existing OS tool.
* **Persisted, closer to Disk Usage's posture than Duplicate Finder's.** Scan results are saved to a
  new `file_fragmentation` table (§7), not re-read live every time. Unlike Duplicate Finder, this
  feature drives no delete action, so a stale cached fragment count is harmless to redisplay — the
  same reasoning Disk Usage's `filesystem_index` already relies on, not Duplicate Finder's
  never-cache rule (which exists specifically because that output feeds a destructive delete).
  Practical effect: navigating a tab back to an already-scanned folder redisplays the last scan
  instantly; a scan survives the tab navigating away mid-scan (it still finishes and persists) and
  survives an app restart.
* **Windows/NTFS-only in practice.** `FSCTL_GET_RETRIEVAL_POINTERS` (the same primitive `defrag.exe`/
  "Optimize Drives" use internally) is an NTFS volume primitive with no portable equivalent reused
  here. Scoped per-file, not scan-wide: a file on a non-NTFS volume (FAT32/exFAT) or any other
  unexpected per-file `DeviceIoControl` failure is skipped from the report ("fragmentation unknown for
  this file") rather than aborting the whole scan — only a `root` whose drive itself can't be
  enumerated at all fails the call. Non-Windows builds fail every call with `ErrorCode::IoError`, same
  posture as `WindowsDiskUsageScanner`/`IFolderMonitor`.
* **Application:** `FileFragmentationInfo` (`src/application`, plain struct, Application-layer like
  `DuplicateFileGroup`/`DiskUsageNode`): `{ path, sizeBytes, fragmentCount }` — `fragmentCount <= 1` is
  contiguous; a file with zero allocated extents (small enough to be resident in its own MFT record)
  is reported as `1`, not `0`, since that's the most-contiguous case, not an unknown one.
  `FragmentationSummary` (`{ filesScanned, totalBytesScanned, fragmentedFileCount, totalFragments }`)
  is always computed on demand from a file list, never persisted itself — one source of truth.
  `IFragmentationScanner::scan(root, minFileSizeBytes, onProgress, cancelRequested)` (new Port, mirrors
  `IDuplicateFileScanner`'s shape exactly, including a real per-file progress callback and a real
  `std::atomic<bool>` cancel flag). `onProgress(scanned, totalCandidates, currentPath)` fires during
  both scan phases: the initial recursive listing (`totalCandidates == 0`, since the eventual
  candidate count isn't known until it finishes — treated as indeterminate progress) and the
  per-candidate extent-counting pass that follows it (`totalCandidates` == the final count);
  `currentPath` is the entry just discovered (listing) or examined (extent-counting), surfaced by
  `FragmentationViewModel` as the progress card's live "which path is this touching right now" text
  alongside the running/total count. `IFragmentationRepository` (new Port, mirrors
  `IDiskUsageRepository`): `saveScan(scanRoot, minFileSizeBytes, files)` (batched upsert in one
  transaction; rows previously recorded under `scanRoot` with `size >= minFileSizeBytes` but absent
  from `files` this time are deleted — rows *below* that threshold are left untouched, since a later
  scan with a smaller threshold must not make every bigger file look removed) and `filesUnder(folder)`
  (every persisted row strictly under `folder`, empty — not an error — if nothing there was ever
  scanned). `FragmentationUseCase` (new, stateless, depends on **both** Ports — the
  "orchestrates scanning + persistence" shape `DiskUsageUseCase` already established, not Duplicate
  Finder's scanner-only shape): `scanAndPersist(...)` scans and persists only on success (a cancelled
  or failed scan is never persisted, so an interrupted scan can't delete stale rows for files it never
  got to re-examine); `filesUnder(folder)` passthrough, for cache-only redisplay; pure
  `summarize(files)` and `sortedByFragmentCount(files)`.
* **`src/adapters/fragmentation`** (new module, `explorer_adapters_fragmentation`, links
  `explorer_application` + `Qt6::Sql` for the repository half): `WindowsFragmentationScanner`
  implements `IFragmentationScanner`. Enumerates via the existing `listDirectoryRecursive` (giving
  `totalCandidates` up front, same shape as Duplicate Finder's size-bucketing pre-pass), then per
  candidate file: opens a handle via the existing `WindowsLongPath::withPrefix()` helper, loops
  `DeviceIoControl(FSCTL_GET_RETRIEVAL_POINTERS, ...)` advancing the starting VCN while the call
  reports `ERROR_MORE_DATA`, and sums `ExtentCount` across calls as the fragment count directly (NTFS
  already reports the minimal extent set — no adjacency/merging logic needed). `cancelRequested`
  polled between files, same file-boundary granularity as
  `StandardDuplicateFileScanner`/`WindowsDiskUsageScanner`. `SQLiteFragmentationRepository` implements
  `IFragmentationRepository` against the new `file_fragmentation` table (§7) over its **own**
  `QSqlDatabase` connection to the same `explorer.db` file, same `PRAGMA busy_timeout` rationale as
  `SQLiteDiskUsageRepository`. Its stale-row delete and its `filesUnder` lookup both match rows
  strictly under a path via the same LIKE-prefix-with-escaped-`%`/`_` idiom
  `SQLiteDiskUsageRepository::saveScan` already established and proved (reused verbatim, since the
  escaping behavior is correctness-critical) — simplified from that precedent by dropping its
  `path = scanRoot` exact-match branch, since this table never stores a row for the scanned
  folder/drive root itself (files only).
* **Threading:** `FragmentationService`/`FragmentationWorker` (`adapters/viewmodels`, new) — a
  synthesis of two existing patterns, since this feature needs both: the lazy own-thread SQLite
  connection `DiskUsageWorker`/`DiskUsageService` establish (`SQLiteFragmentationRepository` wraps a
  thread-affine connection, constructed lazily on the worker thread, not in a constructor that still
  runs on whatever thread constructs the service), and the real per-request
  `std::atomic<bool>` cancel flag + live progress callback `DuplicateFinderService`/
  `DuplicateFinderWorker` establish (needed because the per-file `DeviceIoControl` cost is real,
  unlike Disk Usage's plain `FindFirstFileW` walk, which has no cancel in v1). `requestId` is
  caller-supplied (each per-tab `FragmentationViewModel` uses the `NotificationCenter` `ProgressId` it
  creates for its own scan), so **multiple tabs can each have a scan in flight concurrently** —
  different tabs' requests carry different `requestId`s and simply queue on the one worker thread,
  the same "single dedicated thread serializes concurrent per-tab requests" posture
  `DiskUsageService`/`AdvancedSearchWorker` already accept. `requestScan`'s own result is
  success/failure only — the (potentially huge) scanned list is never marshaled back over the scan
  path, same "persist, then issue a separate cheap lookup for just what you want to show" posture
  `DiskUsageService` established; a caller issues a separate `requestFilesUnder` call afterward, the
  same call a plain navigation-triggered cache lookup uses — one code path renders results either way.
  Owned long-lived by `CompositionRoot`, same lifetime as `DiskUsageService`; `CompositionRoot` holds
  only the scanner + service members (no use case/repository member at that level), mirroring Disk
  Usage's existing wiring exactly.
* **`FragmentationViewModel`** (`adapters/viewmodels`, new) — one per tab, same lazy-start lifetime as
  `FolderSizeViewModel`/`TerminalWidget`. Tracks the bound tab's current path read-only (via
  `TabViewModel::currentPathChanged`) and holds a `NotificationCenter&` it uses directly to
  create/update/terminate its own cancellable progress-bar card per scan — the same "ViewModel calls
  into `NotificationCenter` directly" posture §14.37 established for `TagListViewModel`/
  `WorkspaceController`. `scanFolder(minFileSizeBytes)`/`scanDrive(minFileSizeBytes)` scan the tab's
  current path (or its drive root) as captured when clicked; a no-op while this tab's own scan is
  already in flight (a different tab's concurrent scan doesn't block it). `refreshForCurrentPath()` —
  cache-only redisplay via `requestFilesUnder`, called on construction and on the bound tab's
  `currentPathChanged`, the same posture `FolderSizeViewModel::refreshForCurrentPath` established
  (an unscanned folder returns empty, not an error, in both features alike — no special-casing
  needed). `onScanFinished` just calls `refreshForCurrentPath()` unconditionally on success: correct
  whether or not the tab is still on the scanned root, since it always shows whatever the tab's
  *current* path's cached data now is — no generation/stale-result bookkeeping needed, unlike
  Advanced Search's async scans (§14.38), since persistence makes every completed scan's effect valid
  to show whenever it's next looked at.
* **UI:** `FragmentationWidget` (`src/ui/widgets`, new), mirrors `FolderSizeWidget`'s per-tab
  ownership shape, registered via a third `bottomPanel->addPanelTab(tr("Fragmentation"),
  fragmentationWidget)` call in `WorkspacePaneWidget::addPageForTab`, right after the existing Folder
  Size registration. Layout: a toolbar row (`Scan Folder`/`Scan Drive` buttons — text flips to
  "Scanning..." and both disable for the duration of *this tab's* scan only — plus a minimum-file-size
  spinbox, default 64 KB, higher than Duplicate Finder's 4 KB since a file at or below one NTFS
  cluster can never be fragmented, and a "Show only fragmented files" checkbox, default checked);
  a summary label row (files scanned, total size scanned, fragmented file count/percentage, total
  fragments — answers "how many files and sizes"), refreshed on every `resultsChanged` whether from a
  fresh scan or a cache redisplay; a flat, sortable `FragmentationResultsModel` (new
  `QAbstractTableModel`, simpler than `DuplicateGroupsModel` — no group/member hierarchy, one row per
  file: Name/Folder/Size/Fragments/State) inside a plain `QTreeView` filling the remaining space,
  default-sorted by Fragments descending (answers "what exact files and their fragmentation state").
  Progress reuses the existing cancellable `NotificationCenter` progress-bar mechanism as-is.
  Right-click a row for "Open Containing Folder" — the ViewModel emits a plain path-carrying signal
  that `WorkspacePaneWidget` (which already owns `this` at the wiring site) connects straight to its
  own `navigateAndSelect(path.parent_path(), path)`, the same "emit and let the owning widget call
  back into the pane" posture `FolderSizeViewModel::navigationRequested` established. No rename/tag/
  preview/delete actions — read-only by nature.
* **Not built:** any defragmentation/consolidation action (see above); multi-root/whole-PC scanning
  (one root per scan — the tab's current folder or its drive); SSD detection/warning (fragmentation is
  largely irrelevant on SSDs — out of scope for this pass, the tab reports whatever drive it's
  pointed at regardless of media type); a cluster-size display (dropped — surfacing it consistently
  across both a fresh scan and a cache-only redisplay isn't worth the bookkeeping for a purely
  informational label); a Linux implementation (no portable equivalent reused here, deferred until
  Linux support is actually built, same posture as `IFolderMonitor`); any *live-scanning*
  integration with the Details view — the one addition is §14.53's cached-only Fragments column,
  which reads this feature's persisted results exactly like Disk Usage's own cached sizes (§14.40)
  but never triggers a scan of its own; persisting which bottom-panel tab was last active across restart (unchanged from
  §14.24/§14.40's existing posture — the scan *data* itself persisting is the point of this feature).
  No CTest coverage for `WindowsFragmentationScanner` itself beyond §11's existing OS-shell carve-out
  — reliably producing a genuinely fragmented file inside a test fixture would need either a
  multi-gigabyte churn workload or filesystem-internal trickery, and the IOCTL's behavior is itself
  live, per-volume NTFS state, the same reasoning `WindowsDiskUsageScanner`'s walk already uses this
  carve-out for; smoke-tested manually instead. `FragmentationUseCase`'s logic (including that a
  cancelled/failed scan never calls `saveScan`) gets mocked-Port coverage under `tests/application`;
  `SQLiteFragmentationRepository` gets a real `:memory:`-SQLite adapter test under `tests/adapters`,
  same shape as `SQLiteDiskUsageRepositoryTest`. No automated coverage for
  `FragmentationViewModel`/`FragmentationWidget`/the `WorkspacePaneWidget` wiring, per the existing
  §11 UI/ViewModel-glue carve-out (same as `FolderSizeViewModel`/`FolderSizeWidget`).

### 14.53 Details-View Fragments Column

A `FragmentsColumn` added to `FileListModel` (§14.39's customizable-columns mechanism), showing each
file's NTFS fragment count from §14.52's persisted scan results. **Cache-only, same posture as
§14.40's Size-column integration, not a live scan**: it reads whatever `FragmentationService`
already has on disk from a prior "Scan Folder"/"Scan Drive" run in the Fragmentation tab, via the
same `FragmentationService::requestFilesUnder` cache lookup that tab's own cache-only redisplay uses
— the main Details view never triggers a fragmentation scan of its own. A file never scanned (or
skipped by the scanner, e.g. non-NTFS) shows a blank cell, never a 0; directories always show blank,
since `FileFragmentationInfo` is file-only.

* **`FragmentCountsByPathMap`** (`src/application/FragmentCountsByPath.h`, new) —
  `std::unordered_map<std::filesystem::path, std::uint32_t, PathHash>`, the same shape as
  `FolderSizesByPathMap`. `FileListModel::setFragmentCounts(const FragmentCountsByPathMap&)` (new
  slot, mirrors `setFolderSizes`) stores it and emits `dataChanged` for just `FragmentsColumn`'s
  rows — simpler than `setFolderSizes`, since Fragments isn't a sort criterion (see below), so there
  is no resort branch to mirror.
* **No new `SortCriterion`.** Clicking the Fragments header falls through to
  `criterionForColumn`'s existing `default: SortCriterion::Name` case, exactly like
  `DateCreatedColumn`/`GitStatusColumn`/`TagsColumn` already do — none of those three map to a real
  sort criterion either, so this is existing precedent rather than a new gap.
* **Wiring**: `TabViewModel` gains a `FragmentationService&` constructor dependency (next to its
  existing `DiskUsageService&` one) and a new `requestFragmentCounts(path, entries)`, wired into the
  same `directoryContentsChanged` funnel as `requestFolderSizes`, right after it. Same stale-reply
  guard (`path != currentPath()`) and same "no subdirectories → skip the round trip" shaped fast
  path as `requestFolderSizes`, but keyed on "no plain files" instead (a folder containing only
  subfolders has nothing a fragmentation scan would have recorded). `FragmentationService` — already
  owned long-lived by `CompositionRoot` for §14.52's bottom-panel tab — is threaded through the same
  three call sites `DiskUsageService&` already flows through: `WorkspacePaneViewModel`'s `addTab()`,
  `WorkspaceController`'s four pane constructions, and `CompositionRoot`'s
  `createTabViewModel`/`createWorkspacePaneViewModel`/`createWorkspaceController` factories.
* **Column visibility**: `FragmentsColumn` is appended as `FileListModel::ColumnCount`'s new last
  entry (not inserted — every other column's numeric index must stay unchanged so existing users'
  persisted column-width/order/visibility arrays, which are positional, don't silently shift). Most
  of the visibility/width/order plumbing (`ColumnVisibilitySettings`, `AppConfig::detailsColumnWidths`/
  `detailsColumnOrder`, `FileBrowserView`'s header right-click menu) is already generic over
  `ColumnCount` and needed no change; `SettingsDialog`'s `kAllVisibilityColumns`/`columnDisplayName()`
  and `ColumnVisibilitySettingsStore`'s `kAllColumns`/`toString()` are the two explicit per-column
  label tables that each gained a `FragmentsColumn` entry.
* **Not built:** no on-demand scanning triggered from the Details view; no change to §14.52's own
  `FragmentationResultsModel`/`FragmentationWidget` (that tab already has its own Fragments column).
  No new CTest coverage — `FileListModel`/`TabViewModel`/`WorkspacePaneViewModel`/`WorkspaceController`
  are UI/ViewModel-glue, already under §11's existing carve-out, same posture as the Size/Percent
  column wiring; verified by manual smoke test instead.

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

### 14.55 Bulk/Batch Rename

A new, self-contained tool (same posture as Duplicate Finder §14.51/Fragmentation §14.52) for
renaming several files/folders in one action — find/replace (plain or regex), a sequential counter,
a case transform, and an extension override, combined into one pattern applied to every selected
item with a live before→after preview before anything touches disk. Reachable two ways: the item
context menu's new "Bulk Rename..." entry (enabled only once 2+ items are selected, same precedent
as Cut/Copy/Delete already acting on the whole selection — single-item rename stays F2/"Rename",
§14.13.6), and a new `Tools > Bulk Rename...` menu entry (next to `Find &Duplicate Files...`) that
uses the current selection if it's 2+ items, otherwise falls back to the focused tab's whole
current-folder listing.

* **`BulkRenamePattern`** (`src/domain`, new) — a pure, stateless struct holding the pattern
  (find/replace text + regex/match-case toggles, case transform, counter start/step/digit-
  padding/position, optional extension override) plus its own `apply(stem, extension, indexInBatch)`
  logic, applying four fixed stages in order (find/replace → case transform → counter → extension) —
  same free-standing, UTF-8-in/out style as `GlobMatch`/`RegexMatch`/`WholeWordMatch`. Regex mode
  compiles `findText` as a `std::wregex` (ECMAScript, same as `RegexMatch`) and substitutes via
  `std::regex_replace`, so `replaceText` may use `$1..` backreferences; an invalid pattern returns
  `Result::failure` rather than propagating, same error boundary `RegexMatch::matches` already uses.
* **`BulkRenameUseCase`** (`src/application`, new) — stateless, depends only on
  `IFileSystemRepository` (never on another use case, same rule `FileTransferUseCase`/
  `FilePreviewUseCase` already follow). `computePreview(selection, pattern)` runs `pattern.apply`
  per item and classifies each result: `InvalidName` for the same forbidden-character set
  `FileNameEditDelegate`'s `forbiddenCharsPattern` already blocks client-side; `Unchanged` when the
  proposed name equals the current one; `CollisionWithinBatch` when two proposed names match
  (case-insensitive, via `UnicodeCaseFold`, same comparator `GlobMatch` uses); `CollisionWithExisting`
  when `IFileSystemRepository::stat` finds an unrelated sibling already at that name; otherwise `Ok`.
  Deliberately does **not** attempt a two-phase/temp-name shuffle for a same-batch name swap — such a
  case is always flagged as a collision and left for the user to resolve (adjust the pattern, or
  rename in two passes), the same "refuse rather than get clever" posture as the git-checkout-
  overwrite refusal (Specification.md §2.1). Has no `execute()`: applying the renames reuses
  `FileOperationsController` below so the existing per-rename tab/folder-tree repoint machinery
  (§14.41) keeps firing unchanged.
* **`FileOperationsController::bulkRename`** (new slot) — applies a list of (source, destination)
  pairs via the same `FileNavigationUseCase::moveFile` single-item call `renamePath` already uses,
  emitting `pathRenamed` per successful item and one batched `directoryContentsMayHaveChanged` at the
  end, same aggregated-error posture `pasteInto` already uses for a multi-item batch.
* **`BulkRenameViewModel`** (`adapters/viewmodels`, new) — thin ViewModel over the use case, same
  "thin-dialog-over-a-ViewModel" shape as `TagManagerViewModel`/`ManageFavoritesDialog`'s
  `FavoritesViewModel`: `updatePattern` recomputes the preview and emits `previewChanged` on every
  pattern edit; `apply` filters to `Ok` entries and calls `FileOperationsController::bulkRename`.
  Constructed transiently per dialog show, not owned long-lived by `CompositionRoot` — same posture
  as `TagManagerViewModel` in `MainWindow::showTagManagerDialog`.
* **`BulkRenameDialog`** (`src/ui/widgets`, new) — modal, pattern controls above a live preview
  table (Original Name | New Name) rebuilt on every `previewChanged`, same "rebuild on signal"
  posture as `TagManagerDialog::rebuildTagChips`. A non-`Ok`/`Unchanged` row gets a warning
  background and a tooltip naming the reason; "Rename" is disabled while any row has one.
* **Wiring**: `FileItemContextMenu::Callbacks` gains `bulkRename`; its "Bulk Rename..." `QAction` is
  enabled when `paths.size() > 1 && callbacks.bulkRename`, same `setEnabled` pattern as
  `renameAction`/`openAction`. `WorkspacePaneWidget::showItemContextMenu` wires it to a new
  `bulkRenameRequested` signal bubbled up to `MainWindow` (same style as `navigationFailed`);
  `FolderTreeView`'s context menu leaves it unset, since that selection is always exactly one folder.
  `MainWindow::showBulkRenameDialogFromMenu` (the Tools-menu entry point) uses
  `tab->selectedEntries()` when it has 2+ items, else
  `m_workspaceController->fileNavigationUseCase().listDirectory(tab->currentPath())` — the same
  use-case accessor `showDuplicateFinderDialog` already uses — for the whole-folder fallback. Both
  entry points converge on one private `showBulkRenameDialogFor(selection)`.
* **`CompositionRoot`** gains one new long-lived member, `BulkRenameUseCase`, constructed next to the
  other `IFileSystemRepository`-only use cases.
* **Not built:** no saved/reusable rename patterns across sessions; no recursive bulk rename into
  subfolders (operates on one folder's listing/selection only, same scope as the multi-selection it
  extends); no undo (consistent with the existing v1 scope exclusion, Specification.md §5).
* **Testing**: `BulkRenamePattern` gets plain unit tests (`tests/domain`, no fixtures, per §11) for
  each stage and the fixed stage order; `BulkRenameUseCase::computePreview` gets mocked-`IFileSystemRepository`
  coverage (`tests/application`, GoogleMock, no Qt/SQLite/disk) for every `EntryStatus` outcome.
  `BulkRenameViewModel`/`BulkRenameDialog` and the context-menu/Tools-menu wiring get no new CTest
  coverage — UI/ViewModel glue, the same §11 carve-out already applied to
  `FolderSizeViewModel`/`FolderSizeWidget` and `FragmentationViewModel`/`FragmentationWidget`;
  verified by manual smoke test instead.

### 14.56 Advanced Search: Match-Mode Toggles, Consistent Empty-Criteria Folder View

Two related fixes to §14.19/§14.20's Advanced Search pane, bringing it in line with quick search
(§14.42/§14.44/§14.45).

* **`SearchCriteria` gains a `NameSearchOptions nameOptions` member** (`src/application/
  SearchCriteria.h`). `FileNavigationUseCase::filterByCriteria` now calls the options-aware
  `filterByName(files, query, options)` overload (§14.44) instead of the plain 2-arg one — the
  only change needed at the pure-filter layer, since that overload already existed for quick
  search and default-constructs to the historical Contains/case-insensitive behavior.
* **`SearchCriteriaPanel` gains the same 5 toggle buttons as `SearchPanelWidget`**: Match Case
  (`Aa`), Whole Word (`ab`), Exact Name (`=`), Match Regex (`.*`), and Skip Git-Ignored (`.gi`),
  in a new row between the Name/size fields and the tag-search box. Whole Word/Exact Name/Regex
  are mutually exclusive with each other (and deselectable by re-clicking the active one), Match
  Case and Skip Git-Ignored are independent — the exact same hand-rolled "exclusive but
  deselectable" logic `SearchPanelWidget::onMatchModeButtonToggled` uses, for the same reason
  (this vendored Qt build's `QButtonGroup` has no `ExclusionPolicy::ExclusiveOptional`). Object
  names are distinct from the Search tab's own 5 buttons (`advancedSearchMatchCaseButton`, etc.)
  since both panels can be visible at once. No new signal: toggling any of the 5 routes through
  the existing `onAnyFieldEdited` → `criteriaEdited(currentCriteria())` path, same as editing
  Name/Min size/Max size — `currentCriteria()` now also reads `nameOptions` from button state, and
  `setCriteria()` resyncs all 5 buttons' checked state (signal-blocked) alongside the other fields.
* **Skip Git-Ignored forces a rescan, same as quick search's `setSearchOptions`.**
  `TabViewModel::updateAdvancedSearchCriteria` compares the incoming `nameOptions.skipGitIgnored`
  against the previous value and passes `forceRescan=true` to `runAdvancedSearch` when it changed
  — pruning changes what the recursive walk *visits*, so neither a pruned nor unpruned prior
  snapshot can serve the other state from memory. `startAdvancedSearch` already force-rescans
  unconditionally (Search button/Enter), so it needs no extra handling. `runAdvancedSearch`'s
  non-tag branch now forwards `m_advancedSearchCriteria.nameOptions.skipGitIgnored` as
  `requestRecursiveScan`'s existing (previously unused here) `skipGitIgnored` argument. The
  tag-first branch is unaffected — it answers from the tag database, not a folder walk, so
  Skip Git-Ignored has no effect when tag criteria drive the result set.
* **Empty criteria now always shows the current folder, never "every file scanned so far."**
  Previously, only the pane's *initial* open (`showAdvancedSearchPanel`) showed a plain
  non-recursive listing of the current folder; clearing every field back to empty while already
  active fell through to `filterByCriteria`'s all-no-op behavior, which surfaced the *entire*
  cached recursive-scan snapshot instead. `showAdvancedSearchPanel`'s current-folder-listing body
  is now a shared private `TabViewModel::showCurrentFolderNonRecursive()`, additionally resetting
  every piece of cached scan state (`m_advancedSearchSnapshot`/`m_advancedSearchPartialSnapshot`/
  `m_advancedSearchSnapshotTags`/`m_advancedSearchRoot`/`m_advancedSearchCachedTagIds`/
  `m_advancedSearchFullScanDone`/`m_advancedSearchScanPending`, and bumping
  `m_advancedSearchGeneration` to discard any scan still in flight) so a later non-empty criteria
  always triggers a fresh scan rather than reusing a stale one — the same cleanup
  `exitAdvancedSearch` does, minus flipping `m_advancedSearchActive` off. It emits
  `advancedSearchResultsChanged`/`advancedSearchCriteriaChanged` at the end, the same tail
  `applyAdvancedSearchFilter` uses, so the status bar count and the tag-chip row stay correct even
  when reached via a live edit rather than the pane's initial open. Called (guarded by the
  existing `isCriteriaEmpty` helper) from `startAdvancedSearch`, `updateAdvancedSearchCriteria`,
  and `removeTagSearchCriterion` (removing the last tag criterion with nothing else set) in
  addition to `showAdvancedSearchPanel` itself; `addTagSearchCriterion` needed no change, since
  adding a tag can never leave criteria empty.
* **Highlight case-sensitivity**: `SearchResultsPane::setHighlightQuery` gains a
  `caseSensitive = false` parameter forwarded to `FileBrowserView::setNameHighlightQuery` (which
  already supported it); `SearchResultsPane::setCriteria` and the `advancedSearchResultsChanged`
  connection in `WorkspacePaneWidget::addPageForTab` pass `criteria.nameOptions.caseSensitive`
  through, matching how `SearchPanelWidget` already threads its own Match Case state into quick
  search's result highlighting.
* **Not built:** Skip Git-Ignored pruning the tag-first branch (no folder walk happens there to
  prune); persisting the 5 toggles' state across restarts (matches every other Advanced Search
  field's existing exclusion, §14.19).
* **Testing**: `FileNavigationUseCaseTest.cpp` gets a new `filterByCriteria` case asserting
  `nameOptions` (e.g. `WholeWord`/`ExactName`) combines correctly with a size bound, mirroring the
  existing options-aware `filterByName` coverage. The `TabViewModel`/`SearchCriteriaPanel` changes
  stay under the existing §11 UI/ViewModel-glue carve-out — verified by manual smoke test.

### 14.57 Environment Variables (View/Edit, Per-Process)

A new `Tools > Environment Variables...` entry (next to `Find &Duplicate Files...`/
`&Bulk Rename...`) opens a dialog managing a small custom set of environment variables scoped to
this running Exp-LORer process only — deliberately **not** the Windows System or User environment
variables (no registry/`setx` writes, no `ExpandEnvironmentStringsW`-visible-everywhere ambition).
Each variable has a name plus an ordered list of values joined with `;` for display/storage.

* **`src/adapters/config/EnvironmentVariable.h`** (new) — plain value type: `QString name;
  QStringList values;`, no Qt object, same posture as `ColumnVisibilitySettings`.
* **`src/adapters/config/EnvironmentVariablesStore.h/.cpp`** (new) — `load()`/`save()` of
  `std::vector<EnvironmentVariable>`, built on the existing `JsonFileStore::tryLoad`/`trySave`
  tolerant-load/atomic-save helper, identical shape to `FavoritesStore`. Persists to
  `environment_variables.json`, alongside `favorites.json`/`config.json` under `appDataDir` (§9).
* **`src/adapters/config/EnvironmentVariableApplier.h/.cpp`** (new) — two free functions (not
  Qt-widget-dependent, so independently testable):
  * `apply(const std::vector<EnvironmentVariable>&, const std::vector<QString>& previouslyAppliedNames)`:
    clears every name in `previouslyAppliedNames` via `SetEnvironmentVariableW(name, nullptr)` first
    (so a removed/renamed variable doesn't linger), then sets each current variable via
    `SetEnvironmentVariableW(name, values.join(";"))`.
  * `currentProcessEnvironmentSnapshot()`: reads every variable already in the process's own
    environment via `GetEnvironmentStringsW`, splitting each value on `";"` the same way `apply`
    joins values back together, and skipping Windows' hidden per-drive pseudo-variables (name
    starting with `"="`, e.g. `"=C:=C:\Users\..."`). Used by `CompositionRoot` (below) to seed the
    store the first time it's empty, so the dialog opens showing the variables that already exist
    (`PATH`, `TEMP`, ...) instead of blank — see **Seeding** below.

  Both `#ifdef _WIN32`-guarded; the non-Windows branch of each is a no-op/empty, same "Windows-only
  for now" posture as §14.54's `expandEnvironmentVariables` (Specification.md §1.2).
* **`src/adapters/config/EnvironmentVariablesViewModel.h/.cpp`** (new, `QObject`) — same
  constructor shape as `FavoritesViewModel(Favorites&, FavoritesStore&, QObject*)`. Applies the
  loaded set once in its own constructor (before `CompositionRoot::createWorkspaceController()`
  ever runs, so before any pane/terminal can exist) and again on every `save()`. Deliberately has
  **no** granular add/remove/move mutators the way `FavoritesViewModel` does: the dialog below is
  transactional (explicit OK/Cancel, unlike Favorites' immediate-apply), so it keeps its own
  working copy and calls `save(std::vector<EnvironmentVariable>)` exactly once, on OK — mirroring
  Favorites' per-click-persists shape would fight the Cancel requirement.
* **Why terminals need no code change** — `WindowsConPtyProcess::start()` (backing the integrated
  terminal, §14.24) already calls `CreateProcessW` with `lpEnvironment = nullptr`, which means the
  spawned shell inherits this process's current environment block automatically. Since
  `EnvironmentVariableApplier::apply` calls `SetEnvironmentVariableW` on the real process
  environment, every terminal started *after* a save already sees these variables with zero
  changes to `WindowsConPtyProcess`/`TerminalWidget`/`TerminalSessionsWidget`. A terminal already
  running when a variable is edited does not retroactively pick up the change — expected OS
  process-creation-time semantics, not a gap. Side benefit, not a tested requirement: address-bar
  `%VAR%` expansion (§14.54) also sees these variables for free, since it resolves through the same
  process environment block.
* **`src/ui/widgets/EnvironmentVariablesDialog.h/.cpp`** (new) — `QTableWidget` (Variable | Value,
  value column showing `values.join(";")`), a right-aligned New/Edit/Delete button row below it
  (Edit also fires on double-click; Delete needs no confirmation, same posture as
  `ManageFavoritesDialog`'s Remove), and OK/Cancel below that. Operates on a local copy of
  `viewModel->variables()`; New/Edit/Delete only touch that copy; OK calls `viewModel->save(...)`,
  Cancel discards it untouched. Name is validated non-empty and case-insensitive-unique against
  sibling rows (Windows variable-name semantics) before a New/Edit is accepted.
* **`src/ui/widgets/EditEnvironmentVariableDialog.h/.cpp`** (new) — nested editor: a name
  `QLineEdit` plus a values `QListWidget` with a vertical New/Edit/Delete/Move Up/Move Down button
  column, same layout idiom as `ManageFavoritesDialog`'s button row, just stacked next to a list
  instead of under a tree; New/Edit a value via `QInputDialog::getText`, Move Up/Down swap adjacent
  rows the same way `ManageFavoritesDialog::onMoveUpClicked`/`onMoveDownClicked` do. Standard
  OK/Cancel at the bottom.
* **`CompositionRoot`** gains `EnvironmentVariablesStore`/`std::vector<EnvironmentVariable>`/
  `EnvironmentVariablesViewModel` members (constructed from `appDataDir / "environment_variables.json"`),
  grouped with the other presentation-only persisted-preference blocks (Favorites/Notification
  Settings/Column Visibility), plus an `environmentVariablesViewModel()` accessor; `main.cpp` passes
  it into `MainWindow`'s constructor the same way `favoritesViewModel()` already is.
* **Seeding**: a plain first pass at this feature left the store empty on a fresh install — the
  dialog opened with nothing in it, which is surprising for a tool framed as "Environment
  Variables" rather than "my custom extra variables." `CompositionRoot`'s private
  `loadOrSeedEnvironmentVariables(EnvironmentVariablesStore&)` helper now calls
  `EnvironmentVariablesStore::load()` and, only if that comes back empty, replaces it with
  `EnvironmentVariableApplier::currentProcessEnvironmentSnapshot()` and immediately `save()`s it —
  so the *first* time the store is empty (a fresh install, or every variable having been deleted),
  it's seeded once from whatever the process already inherited from Windows, and that snapshot
  becomes the persisted, user-editable baseline from then on (never recomputed from the live OS
  environment on a later startup, so edits/deletions stick).
* **Not built:** no System/User (registry-level) environment variable viewing or editing; no
  retroactive update of already-running terminals.
* **Testing**: `EnvironmentVariablesStoreTest.cpp` (tolerant-load + save/load round-trip, same
  temp-dir shape as `FavoritesStoreTest.cpp`/`ColumnVisibilitySettingsStoreTest.cpp`) and
  `EnvironmentVariableApplierTest.cpp` (apply, assert via `GetEnvironmentVariableW`; apply a
  changed set, assert a removed variable is cleared; `currentProcessEnvironmentSnapshot` includes a
  variable set via `SetEnvironmentVariableW` split correctly on `";"`, and never returns a hidden
  `"="`-prefixed pseudo-variable) — the same "deterministic, test-controlled environment variable"
  posture §14.54's `StandardFileSystemRepositoryTest.cpp` already established, not the §11
  OS-shell-side-effect carve-out. `loadOrSeedEnvironmentVariables`, `EnvironmentVariablesViewModel`,
  and both dialogs get no new CTest coverage — the first is a one-line `CompositionRoot`-local
  helper exercising already-tested `EnvironmentVariablesStore`/`EnvironmentVariableApplier` calls,
  the rest are UI/ViewModel glue, same carve-out already applied to
  `FavoritesViewModel`/`ManageFavoritesDialog`; verified by manual smoke test instead.

### 14.58 Preview Panel: Dedicated Per-Kind Widgets

Pure internal restructuring of `PreviewPanelWidget` (§14.26) — no behavior change, no Domain/
Application/Port change. Before this, `PreviewPanelWidget` built and owned every preview kind's Qt
widgets directly (an image/poster composite page, a `QPlainTextEdit`, a `QListView` +
`FolderPreviewListModel`, plus loading/unsupported/error `QLabel`s) and held all the per-kind
rendering logic (`showImage`/`showText`/`showFolder`/`rescaleImageLabel`) itself, dispatched from
one `onPreviewReady()` switch. Extending that file for a future preview kind meant editing its
constructor, member list, and switch statements all at once.

* **`MediaPreviewWidget`** (new, `src/ui/widgets`) — a composite `QWidget` (info `QLabel` + pixmap
  `QLabel` + zoom `QLabel`, same three-label layout the old image page used) covering both
  `FilePreviewKind::Image` and `::Video`, since their rendering is otherwise byte-identical (same
  JPEG-decoded poster bytes; video only adds a bitrate suffix to the info line). Exposes
  `bool setPreview(const FilePreview&)`, returning `false` on a decode failure (null pixmap) so the
  caller falls back to its own shared "unsupported" label rather than this widget needing to know
  that label exists. Owns its own `resizeEvent` override to rescale its pixmap/recompute the zoom
  label — `PreviewPanelWidget::resizeEvent` is gone entirely now that nothing else needs it.
* **`TextPreviewWidget`** (new) — subclasses `QPlainTextEdit` directly rather than wrapping one,
  the same "dedicated widget that just *is* a specialized Qt view/edit control" pattern
  `FolderTreeView` (§14.41) already established for `QTreeView`. Owns its own `SyntaxHighlighter`
  (§14.27), constructed from the `SyntaxHighlightEngine&`/`HighlightThemeViewModel&` it now takes
  directly instead of `PreviewPanelWidget` plumbing them through. Exposes `setPreview(const
  FilePreview&)`.
* **`FolderPreviewWidget`** (new) — subclasses `QListView` directly, same pattern; owns its own
  `FolderPreviewListModel` (unchanged). Exposes `setPreview(const FilePreview&)`.
* **`PreviewPanelWidget`** is now a thin coordinator: constructs the three widgets above plus the
  three status `QLabel`s, and `onPreviewReady()` just calls the matching widget's `setPreview()`
  and switches the stack to it (falling back to the unsupported label when `MediaPreviewWidget::
  setPreview` returns `false`). All objectNames (`previewImageLabel`, `previewImageInfoLabel`,
  `previewZoomLabel`, `previewTextView`, `previewFolderList`, `previewLoadingLabel`,
  `previewUnsupportedLabel`, `previewErrorLabel`, `previewPanelWidget`) are unchanged, so
  `ui_tests/framework/automation_id.py` needed no edits.
* **Not built / changed:** no new preview kind, no `FilePreviewViewModel`/`FilePreviewUseCase`/
  `FilePreview` change — this section is UI-layer only. `MainWindow`'s construction of
  `PreviewPanelWidget` is unchanged (same public constructor signature).
* **Testing**: no CTest coverage, consistent with §11's "UI is not covered by CTest in v1" — same
  carve-out the original §14.26 widget already fell under. `ui_tests/` needed no changes since
  every automation id resolves by objectName + Qt widget type, both unchanged.

### 14.59 Preview Panel: Media Layout Rework (Name/Image/Properties/Zoom) + Smaller Folder Icons

`MediaPreviewWidget` (§14.58) packed the filename, resolution, and (for video) bitrate into one
info line above the image, and centered the image both horizontally and vertically in the space
below it. Reworked into four clearly separated rows, top to bottom: the full filename alone, the
poster/thumbnail (top-aligned, `Qt::AlignHCenter | Qt::AlignTop`, no longer vertically centered), a
properties block, and the unchanged zoom label.

* **`MediaPreviewWidget`**: `m_infoLabel` (objectName `previewImageInfoLabel`) split into
  `m_nameLabel` (`previewImageNameLabel`, filename only, bold and two points larger than the
  default label font) and `m_propertiesLabel` (`previewImagePropertiesLabel`, one line each for
  Resolution, Length (`::Video` only, `metadata.duration()` formatted `mm:ss`/`hh:mm:ss`), Bitrate
  (`::Video` only), and Size, each shown only when the underlying value is known/positive), the
  latter now wrapped in a `QGroupBox` titled "Properties"
  (`previewImagePropertiesGroupBox`) instead of sitting bare in the layout. `m_imageLabel`'s
  alignment changed from `Qt::AlignCenter` to `Qt::AlignHCenter | Qt::AlignTop`.
  `ui_tests/framework/automation_id.py` defines neither old nor new info-label/group-box id (only
  `PREVIEW_IMAGE_LABEL`), so no edit needed there.
* **Shrink responsiveness fix**: `m_imageLabel` previously kept Qt's default size policy, so its
  `sizeHint()`/`minimumSizeHint()` tracked whatever pixmap `rescaleImage()` had last set (i.e. the
  prior, larger panel size) — shrinking the right panel's splitter couldn't make incremental
  progress against that stale hint and instead jumped straight to the overall minimum on release.
  Expanding was unaffected since a growing hint never blocks growth. Fixed by setting
  `QSizePolicy::Ignored` on both axes plus an explicit `setMinimumSize(1, 1)`, so the layout no
  longer consults the label's size hint at all and `resizeEvent()` → `rescaleImage()` tracks the
  splitter continuously in both directions.
* **Fixed-width fix**: `m_nameLabel`/`m_propertiesLabel` kept Qt's default (`Preferred`, which has
  `ShrinkFlag` set) horizontal size policy, so per `qSmartMinSize()` the layout's floor for each was
  `minimumSizeHint().width()` — for a word-wrapped `QLabel`, that's the width of its widest
  *unbreakable* run of characters. A long filename (or property value) with no spaces can't wrap at
  all, so its minimum width was its full unwrapped text width, forcing the whole right panel/splitter
  wider whenever such a file was previewed, and it never shrank back (splitters only grow to satisfy
  a rising minimum). Fixed by setting the horizontal policy on both labels to `QSizePolicy::Ignored`
  (vertical policy unchanged), which drops that floor to 0 while word-wrap/`heightForWidth` still
  reflow the text correctly within whatever width the layout actually hands it.
* **`FilePreview`** (Domain, additive): `image()`/`video()` now take a `std::uintmax_t
  fileSizeBytes` parameter, exposed via new accessor `fileSizeBytes()` (0 for every other kind) —
  the same graceful-degradation posture as the rest of the entity, not a reshape of its existing
  shape. `FilePreviewUseCase::generatePreview()` threads `target.size()` (already stat'd as part of
  the `FileNode` passed in — no new filesystem call) through to both factory calls.
* **`FolderPreviewWidget`** (§14.26): now calls `setIconSize(QSize(16, 16))` and
  `setUniformItemSizes(true)` on construction — folder-preview rows previously used
  `QListView`'s unset (style-default, visually large) icon size; 16×16 matches the icon size
  `FileBrowserView` already uses for its own `ViewMode::List`/`::Details`.
* **Not built/changed**: no new `FilePreviewKind`, no `IMediaDecoder`/`IFileSystemRepository`
  change, no `FilePreviewViewModel` change.
* **Testing**: `tests/domain/FilePreviewTest.cpp` and
  `tests/application/FilePreviewUseCaseTest.cpp` updated for the new `fileSizeBytes` parameter/
  accessor. No UI test changes (automation ids above unaffected); UI is still not covered by CTest
  in v1 (§11).

### 14.60 Workspaces: Named Session Profiles + Per-Workspace Multi-Instance

Generalizes the single, fixed window session (§14.14 — layout, each pane's tabs/paths/view
mode/sort order, panel state, window geometry, all the `AppConfig` struct already captured) into
any number of **named** session snapshots ("workspaces"), browsable/launchable from a new File menu
section, each one openable as its own OS process running concurrently with the others. Reuses
`AppConfig`/`WorkspaceConfig` and `AppConfigStore`'s JSON shape verbatim — a workspace is just an
`AppConfig` with a name and an id, nothing new to serialize.

* **Storage** (`src/adapters/config/WorkspaceStore.h/.cpp`, new `WorkspaceEntry{id, name}`): under
  `appDataDir/workspaces/`, `index.json` holds the `[{id, name}, ...]` registry (written only by
  `WorkspaceStore::createWorkspace`/`renameWorkspace`/`deleteWorkspace`); `<id>.json` holds that
  workspace's own `AppConfig`, read/written by a plain `AppConfigStore` pointed at that path — zero
  changes to `AppConfigStore` itself. Keeping the registry and the per-workspace session in separate
  files means a rename from the Workspace Management dialog (below) can never be clobbered by some
  other, already-running instance's save-on-exit, since that instance's `AppConfigStore::save` never
  touches `index.json`. A pre-existing single `config.json` is migrated once into
  `workspaces/default.json` (id `"default"`, name `"Default"`) the first time `index.json` doesn't
  yet exist; an empty/missing index self-heals to that same single `"default"/"Default"` entry, the
  same tolerant-fallback posture `AppConfigStore::load()` already has for a missing file.
* **`CompositionRoot`**: gains a `WorkspaceStore m_workspaceStore` member, declared before
  `m_appConfigStore` so migration runs first; `m_appConfigStore` is now constructed from
  `m_workspaceStore.configFilePath(workspaceId)` instead of the fixed `appDataDir / "config.json"`.
  Constructor takes the resolved workspace id alongside `appDataDir`.
* **Per-workspace single instance** (`src/app/WorkspaceInstanceGuard.h/.cpp`): a Windows named mutex
  plus a `QLocalServer`/`QLocalSocket` activation channel, both named from a hash of
  `(appDataDir, workspaceId)` — the existing §6 "name must derive from the resolved data dir" rule,
  extended to also fold in the workspace id. `main.cpp` tries to acquire the guard right after
  constructing `ExpLorerApplication` (needs a `QCoreApplication` for `QLocalSocket`); losing that
  race means some other process already owns this exact workspace, so this process sends one
  "activate" message over the matching socket (the owner's server raises/focuses its window) and
  exits before `CompositionRoot`/`MainWindow` are ever constructed. Two different workspace ids (or
  two different `--data-dir` overrides) derive different names and simply never contend with each
  other — this is the whole mechanism behind running multiple workspaces concurrently without
  conflict; resources that stay genuinely shared across every workspace in one data directory
  (`explorer.db`, the thumbnail cache, `favorites.json`, etc.) are unaffected and keep relying on
  `PRAGMA busy_timeout` (§5) and atomic `QSaveFile` writes to tolerate concurrent processes, same as
  today. Not implemented for non-Windows yet (`tryAcquire()` always succeeds off Windows), consistent
  with "Windows-first, Linux later" and the existing `#ifdef _WIN32` scoping of
  `resolveDataDirOverride` in `main.cpp` (§9).
* **`main.cpp`**: the wide-argv scanning `resolveDataDirOverride` already used for `--data-dir` is
  generalized into a small shared helper, reused for a new `--workspace <id>` startup flag (default
  `"default"`).
* **`MainWindow`**: the `AppConfig` snapshot `closeEvent()` already built inline is factored into a
  private `captureCurrentConfig() const`, reused by both `closeEvent()` (unchanged behavior) and the
  new Save-as-Workspace action below. Gains two small constructor additions: `WorkspaceStore&` and a
  plain `WorkspaceLaunchContext{appDataDir, workspaceId}` (`src/ui/widgets/core/
  WorkspaceLaunchContext.h`) rather than further raw path/string parameters. Window title now shows
  the current workspace's name, so concurrently open windows are distinguishable.
* **File menu** (`createMenuBar()`, between the existing Settings... and Exit):
  * **Workspaces ▸** — submenu rebuilt on `aboutToShow` (same pattern as the Favorites menu,
    `rebuildFavoritesMenuItems`) from `WorkspaceStore::listWorkspaces()`, current workspace checked.
    Clicking the entry for the workspace already open *in this window* just raises `this`; clicking
    any other entry runs `QProcess::startDetached(QCoreApplication::applicationFilePath(),
    {"--data-dir", appDataDir, "--workspace", id})`. If that workspace is already open elsewhere, the
    new process simply loses the instance-guard race, forwards activation to the real owner, and
    exits — correct by construction, with no separate pre-check needed (a pre-check would itself
    race against the same condition).
  * **Save as Workspace...** — `QInputDialog::getText` for a name; an existing name prompts an
    overwrite confirmation (reuses that id), otherwise creates a new id via
    `WorkspaceStore::createWorkspace`. Writes `captureCurrentConfig()` to that workspace's
    `AppConfigStore`. Deliberately does not rebind the current window to the saved-to workspace — it
    stays tied to whatever id it was launched with, and keeps saving back there on close.
  * **Workspace Management...** — new `WorkspaceManagerDialog` (`src/ui/widgets/core/`, same
    list+buttons shape as `ManageFavoritesDialog`/`TagManagerDialog`): list of workspaces with
    Rename/Delete/Close. Delete is blocked with a message when that workspace's instance-guard name
    is currently held (i.e. it's open in another window right now), checked via a non-destructive
    probe. Rename is always safe — it only ever touches `index.json`, which no running instance's own
    save path writes to (see Storage, above).
* **Build**: `vcpkg.json` adds `"network"` to `qtbase`'s feature list; root `CMakeLists.txt`'s
  `find_package(Qt6 REQUIRED COMPONENTS ...)` gains `Network`; `src/app/CMakeLists.txt` links
  `Qt6::Network` for `WorkspaceInstanceGuard`.
* **Testing**: `tests/adapters/WorkspaceStoreTest.cpp` (migration from a legacy `config.json`,
  create/list/rename/delete, empty-index self-heal, id stability across rename) — same temp-directory
  style as `tests/adapters/AppConfigStoreTest.cpp`. `WorkspaceInstanceGuard`'s OS-mutex/IPC half is a
  "Carve-out — OS-shell side effects with no assertable return" (§11, same posture as
  `ShellContextMenuProvider`) — manually smoke-tested, not CTest-covered; its key-derivation step is
  still a plain, independently testable pure function. UI is still not covered by CTest in v1 (§11).

### 14.61 Media Viewer Mode

A new per-tab mode (View menu, "Media Viewer Mode," checkable) that replaces the pane's normal
browsing with a full-grid gallery of every image or video found recursively under whatever folder
the tab was showing when the mode was entered, filterable by AND-combined tags and a name/toggle
filter from a new right-panel "Searcher" tab. Entirely UI/Adapters — reuses `AdvancedSearchService`'s
existing recursive-scan/tag-first-candidate requests, `TagManagementUseCase::findFilesWithAllTags`
(§14.20's "tag-first" design), and `MediaExtensions::isImageExtension`/`isVideoExtension`; no new
Port or Domain/Application change.

* **`MediaViewerKind`** (`src/adapters/viewmodels/media/MediaViewerKind.h`, new): `enum class
  MediaViewerKind { Image, Video }`, same minimal posture as `ViewMode.h`.
* **`TabViewModel`** gains a third per-tab `FileListModel* m_mediaViewerResultsModel`, alongside
  `m_fileListModel`/`m_advancedSearchResultsModel`, plus `mediaViewerActive()`/`enterMediaViewer()`/
  `exitMediaViewer()`/`setMediaViewerKind()`/`addMediaViewerTagFilter()`/
  `removeMediaViewerTagFilter()`/`setMediaViewerNameFilter()` and matching
  `mediaViewerModeChanged`/`mediaViewerResultsChanged`/`mediaViewerKindChanged`/
  `mediaViewerCriteriaChanged`/`mediaViewerLoading(Finished)`/`mediaViewerFailed` signals.
  `enterMediaViewer()` captures `currentPath()` as the fixed recursive root, resets kind to `Image`
  and all filters, exits quick/advanced search if active (single-active-mode invariant, same as
  `showAdvancedSearchPanel`), and dispatches the initial candidate fetch. The candidate list (tag-first
  via `findFilesWithAllTags` when tags are applied, otherwise a full `listDirectoryRecursive` scan,
  both through the already-injected `AdvancedSearchService&`) is cached and **only re-fetched when the
  applied tag set changes** — the Images/Videos dropdown and the name/toggle filter re-filter that
  cached list in memory (`MediaExtensions`-based kind filter, then `FileNavigationUseCase::
  filterByName`), mirroring `runAdvancedSearch`'s cached-snapshot split. Deliberately **does not**
  port Advanced Search's partial-snapshot/progress-tick machinery (§14.37/§14.38) — only a single
  `m_mediaViewerGeneration` counter guards against a stale async reply — since this is a narrower,
  usually-fast, single-root query; a plain busy signal pair covers the UI instead of incremental
  results.
* **`FileBrowserView`** gains `DisplayMode::MediaViewerGrid`, alongside the existing
  `AdvancedSearchResults` (§14.19) — same "ignore `setViewMode()`, lock the rendering" shape, but
  locked to the icon-grid (`m_iconDelegate`, `IconMode`, `ExtraLargeIcons` sizing) instead of the
  Details tree. Rename (F2/context menu) stays disabled in this mode too, same guard as
  `AdvancedSearchResults`.
* **`WorkspacePaneWidget::addPageForTab`** adds a third per-tab stack page: a `FileBrowserView` bound
  to `tab->mediaViewerResultsModel()` in `MediaViewerGrid` mode, wired through the same
  `wireBrowserView()` every other page uses (selection/activation/context-menu all come for free, same
  rationale as §14.18). `pickStackPage` now picks page 2 when `mediaViewerActive()`, ahead of page 1's
  `advancedSearchActive()` check. The same signal also hides the tab's `BottomPanelWidget` and disables
  the pane's back/forward/up/refresh/address-bar/view-mode toolbar for the duration — the grid's root
  is fixed at entry, so navigation is locked rather than attempting to re-scope it live; exiting (the
  Searcher tab's "Exit viewer mode" button, or unchecking the View-menu action) returns to normal
  browsing at the same folder.
* **`SidePanelWidget`** gains one new method, `setPanelTabButtonVisible(int, bool)`, so a panel tab's
  header button can be hidden/shown at runtime without touching `addPanelTab`/`activateTab`. The new
  right-panel "Searcher" tab is registered at startup like Tags/Preview/Monitoring but starts hidden,
  only appearing (with Tags/Preview/Monitoring hidden in turn) while any tab has Media Viewer mode
  active, restoring on exit. The left panel is hidden outright (`setVisible(false)`, it's a direct
  `mainSplitter` child, same posture `WorkspaceLayoutWidget` already uses to hide inactive panes).
* **`MediaViewerSearchPanelWidget`** (`src/ui/widgets/media/`, new) is the Searcher tab's content, top
  to bottom: an Images/Videos `QComboBox`, an "Exit viewer mode" button, a `TagChipWidget::
  Kind::Removable` applied-filter row, a tag-search box + `Kind::ReadOnly` matching-tag row (wired to
  `clicked`, exactly `SearchCriteriaPanel`'s `m_tagMatchesLayout` pattern — no "+" button), and the
  same Match Case/Whole Word/Exact Name/Regex/Skip-Git-Ignored 5-button row plus a name field,
  copied from `SearchCriteriaPanel` rather than extracted into a shared widget (this codebase already
  duplicates that row between `SearchPanelWidget` and `SearchCriteriaPanel`, so a third copy follows
  existing precedent rather than introducing a new abstraction).
* **`MainWindow`**: a new checkable View-menu action drives `enterMediaViewer()`/`exitMediaViewer()`
  on `m_workspaceController->focusedTab()`. `createWorkspace()` registers the Searcher tab as the right
  panel's 4th tab (`kRightPanelTabCount` → 4), hidden immediately after registration. `
  onFocusedTabChanged` gets a new `m_mediaViewerTrackedTab` disconnect/connect block — directly
  mirroring the existing `m_searchTrackedTab`/`m_searchPanelWidget` block rather than introducing a
  dedicated ViewModel class — wiring the focused tab's media-viewer signals to
  `m_mediaViewerSearchPanelWidget`'s setters (and the widget's own signals back to that tab), plus
  hiding/showing the left panel and swapping the right panel's visible tab on
  `mediaViewerModeChanged`. The persisted right-panel active-tab-index restore at startup is clamped
  to exclude the Searcher slot, since the mode is never persisted (same posture as search/advanced
  search — only path/ViewMode/sort/active-tab-index survive in `TabConfig`).
* **Not built:** live re-scoping of the grid if the user could somehow navigate mid-session (they
  can't — navigation is locked, see above); persisting Media Viewer mode, the kind selection, or its
  filters across restarts; Advanced Search's progress-tick partial-result UI (an explicit v1
  simplification, see above) — can be added later if a very large recursive scan proves slow enough
  to need it.
* **Testing**: no new GTest coverage — `TabViewModel`/`AdvancedSearchService`/`SearchCriteria` have no
  existing tests either; this layer's Qt-coupled ViewModel orchestration stays under the existing §11
  UI/ViewModel-glue carve-out, verified by manual smoke test.

### 14.62 Media Viewer Mode: Thumbnail Previews & Metadata Overlays

§14.61's grid originally rendered every row through the plain `FileIconDelegate`/`resolveIcon()`
path used everywhere else (the generic Explorer file-type icon via `IconLoadingService`, never a
decoded preview of the image/video's actual pixels). This wires in real decoded thumbnails plus two
small corner metadata labels per tile, reusing the decode/cache plumbing `ThumbnailCache`'s own
header comment says it was "written so the future grid-icon thumbnail feature can reuse it
unchanged" — the same `IMediaDecoder::generateThumbnail`/`extractMetadata` via
`MediaProcessingUseCase` over the already-shared `CachingMediaDecoder`/`ThumbnailCache`
(`CompositionRoot.h`) the Preview panel uses. Entirely Adapters/UI — no new Port, no Domain/
Application/schema/layering change.

* **`MediaThumbnailData`** (`src/adapters/viewmodels/media/MediaThumbnailData.h`, new): `{ QImage
  image; int width = 0; int height = 0; std::chrono::milliseconds duration{}; }`,
  `Q_DECLARE_METATYPE`'d so it can travel through `QVariant` the way `Tag`/`std::vector<Tag>` do.
* **`MediaThumbnailLoadingWorker`/`MediaThumbnailLoadingService`** (`src/adapters/viewmodels/media/`,
  new) mirror `IconLoadingWorker`/`IconLoadingService`'s chunked-request/per-context-staleness shape,
  but with one dedicated `QThread` rather than a pool — the same single-serialized-worker shape every
  other background `*Service` in this codebase uses (`GitStatusService`/`DiskUsageService`/
  `FragmentationService`/`FilePreviewWorker`). `IconLoadingService`'s pool is explicitly justified by
  cheap-but-many synchronous shell/GDI calls that parallelize well; thumbnail decode instead goes
  through the one shared `MediaProcessingUseCase`/`CachingMediaDecoder` instance, so a pool would just
  contend for the same decoder/cache rather than parallelizing real work. Chunks are ~10 entries
  (smaller than `IconLoadingService`'s 20, since decode is heavier per item). Per item:
  `MediaProcessingUseCase::generateThumbnail()` → `QImage::fromData()` (fine off the UI thread — no
  GUI-thread affinity, same as `IconLoadingWorker`'s `QImage` results) + `extractMetadata()`, packaged
  into a `MediaThumbnailData`. `CompositionRoot` gains `MediaProcessingUseCase
  m_mediaProcessingUseCase` (constructed over the existing `m_cachingMediaDecoder`, same posture as
  `m_filePreviewUseCase`) and `MediaThumbnailLoadingService m_mediaThumbnailLoadingService`, plus a
  `mediaThumbnailLoadingService()` accessor threaded through `createTabViewModel()`/
  `createWorkspacePaneViewModel()`/`createWorkspaceController()` and down through
  `WorkspacePaneViewModel`/`WorkspaceController` into `TabViewModel`'s constructor, alongside the
  existing `IconLoadingService&`.
* **`FileListModel`** gains a `MediaThumbnailRole` (`NameColumn` only) returning a
  `MediaThumbnailData` `QVariant` once decoded, a `m_mediaThumbnailsByPath` cache (cleared in
  `setEntries()` alongside `m_iconCache`), and a `setMediaThumbnails(const QHash<QString,
  MediaThumbnailData>&)` slot that merges a batch in and emits a scoped
  `dataChanged({MediaThumbnailRole})` — identical shape to the existing `setIcons()`. The already-
  resolved generic icon (§14.61's original behavior) keeps showing as the placeholder until each
  row's real thumbnail batch arrives, same "placeholder now, real data later" relationship
  `setIcons()` already has with `resolveIcon()`'s cache-miss fallback.
* **`TabViewModel`** gains `requestMediaViewerResultThumbnails(generation, candidates)`, called from
  `onMediaViewerCandidatesReady` right alongside the existing `requestMediaViewerResultIcons` call —
  same generation-guard discard pattern, same `256×256` max size `FileBrowserView.cpp`'s
  `iconSizeFor(ViewMode::ExtraLargeIcons)` already uses for this grid (the UI-layer constant can't be
  referenced directly from the adapters layer, so it's duplicated as a literal with a comment).
* **`MediaGridTileDelegate`** (`src/ui/widgets/media/`, new) replaces `FileIconDelegate` as the
  `MediaViewerGrid` mode's installed delegate in `FileBrowserView`'s constructor
  (`DisplayMode::MediaViewerGrid` branch). Plain `QStyledItemDelegate` — no rename-edit support
  needed, since F2/rename is already disabled in this mode (so no reason to inherit
  `FileNameEditDelegate` the way `FileIconDelegate` does). `paint()` draws `MediaThumbnailRole`'s
  `QImage` aspect-fit/letterboxed when present, otherwise falls back to painting
  `Qt::DecorationRole`'s placeholder `QIcon` the same way `FileIconDelegate::paintIcon()` does; the
  filename caption and dashed selection-rect outline reuse `FileIconDelegate`'s layout math
  (`nameRectFor`/`sizeHint`), copied rather than inherited. Once a thumbnail has arrived, two small
  semi-transparent rounded-rect label chips are drawn anchored inside the thumbnail box's
  bottom-left/bottom-right corners: bottom-left is always `FileSizeFormat::format()`'s size string;
  bottom-right is `"{width}×{height}"` for an image (`MediaExtensions::isImageExtension()`) or the
  mm:ss/hh:mm:ss duration string (copied from `MediaPreviewWidget.cpp`'s formatting) for a video. The
  filename caption stays below the thumbnail, consistent with every other icon/tile delegate — the
  only visual difference between an image and a video tile is the corner label itself, no extra
  play-button glyph.
* **Not built:** any change to normal browsing's or search results' icon/thumbnail rendering;
  animated/scrubbable video preview (poster frame only, matching the existing Preview panel); live
  re-decoding on resize beyond Qt's normal pixmap scaling; persisting any new state across restarts.
* **Testing**: no new GTest coverage, matching §14.61's own precedent for this layer — this layer's
  Qt-coupled ViewModel orchestration stays under the existing §11 UI/ViewModel-glue carve-out;
  `MediaThumbnailLoadingWorker`'s per-item call is a thin pass-through to the already-tested
  `MediaProcessingUseCase`, same posture as `IconLoadingWorker` having no dedicated test.

### 14.63 IMediaDecoder: Merge generateThumbnail/extractMetadata (Performance)

Every known caller of `IMediaDecoder` (`FilePreviewUseCase`, §14.26/§14.62's
`MediaThumbnailLoadingWorker`) always called `generateThumbnail()` and `extractMetadata()` back to
back on the same file. Since the two were separate Port methods, this meant decoding/probing the
source file twice per request: `FFmpegMediaDecoder::extractMetadata()` re-ran a full
`avformat_open_input`/`avformat_find_stream_info` container probe that `generateThumbnail()` had
just done for the frame grab, and `VipsImageDecoder::extractMetadata()` re-read the whole file from
disk a second time. `CachingMediaDecoder` made it worse for images/video alike: it cached
`generateThumbnail()`'s bytes but passed `extractMetadata()` straight through uncached, so even a
thumbnail cache *hit* still paid the full metadata re-decode cost on every call.

* **`MediaThumbnail`** (`src/domain/MediaThumbnail.h`, new): a plain `{ bytes; metadata; }`
  aggregate — `IMediaDecoder`'s one remaining method, `generateThumbnail(path, maxWidth,
  maxHeight) -> Result<MediaThumbnail>`, returns both from the same decode pass.
  `extractMetadata()` is removed from the Port entirely (no caller ever needed metadata without a
  thumbnail).
* **`VipsImageDecoder`**: reads the file once; a cheap `vips_image_new_from_buffer` header-level
  open (libvips decodes pixel data lazily, so this doesn't force a full decode) yields the source
  image's own width/height for `MediaMetadata`, then the same byte buffer feeds
  `vips_thumbnail_buffer` for the actual thumbnail — one file read instead of two.
* **`FFmpegMediaDecoder`**: builds `MediaMetadata` from the same already-open
  `AVFormatContext`/`AVCodecParameters` used for the frame grab, instead of a second
  `avformat_open_input`/`avformat_find_stream_info` probe of the same file.
* **`CachingMediaDecoder`**: the cached blob now holds both the thumbnail bytes and a serialized
  `MediaMetadata` (a small fixed header + codec string + the bytes, (de)serialized entirely inside
  `CachingMediaDecoder.cpp` — `ThumbnailCache` itself stays a plain byte-blob cache, §8). A cache
  hit now returns metadata too without touching the inner decoder at all; an unparsable/corrupt
  blob is treated as a cache miss and regenerated rather than failing the request.
* Both adapters keep the existing graceful-degradation posture: if the metadata half can't be
  built (e.g. `MediaMetadata::create()`'s width/height>0 validation fails), the call still succeeds
  with zero-valued metadata rather than failing the whole thumbnail.
* **Not built**: a metadata-only Port method (no caller needs one); cross-version on-disk cache
  format compatibility (an unparsable old-format entry is just discarded and regenerated, same as
  any other corrupt entry).
* **Testing**: `VipsImageDecoderTest`/`FFmpegMediaDecoderTest` lost their now-redundant
  `ExtractMetadata*` cases (merged into the existing `GenerateThumbnail*` error-path cases);
  `CachingMediaDecoderTest` gained a case asserting a cache hit returns the originally-decoded
  metadata without a second call into the inner mock; `MediaProcessingUseCaseTest`/
  `FilePreviewUseCaseTest` updated to the merged `MockMediaDecoder` signature.

### 14.64 Media Viewer Mode: Video Hover-Scrub Previews

§14.62's video tiles show one fixed poster frame (~10% into the stream). This adds 5 equally-
spaced preview frames per video and lets the cursor's horizontal position over a tile's thumbnail
pick which one shows — the thumbnail's width is divided into 5 equal bands, band *i* (left to
right, 0-based) shows the frame at `(i + 0.5) / 5` of the stream's duration (10%/30%/50%/70%/90%);
band 0 is exactly today's existing poster-frame position. Images are unaffected: still one static
thumbnail, no hover behavior. Entirely Adapters/UI, same posture as §14.61–§14.63 — no new Port,
no Domain/Application redesign.

* **`MediaPreviewFrames.h`** (`src/application/media`, new, pure): `kCount = 5` plus
  `fractionForIndex(int)`, shared by the FFmpeg seek below, the worker's per-video decode loop, and
  the UI hover-band math, so the three can't drift apart.
* **`IMediaDecoder::generateThumbnail`/`MediaProcessingUseCase::generateThumbnail`** gain a last
  `int previewIndex = 0` parameter (ignored by image decoders; selects which of `kCount` frames a
  video decoder grabs). Defaulted so every existing call site that only ever wanted one frame
  (`FilePreviewUseCase`'s Preview-panel calls) compiles and behaves unchanged — `previewIndex = 0`
  reproduces the exact ~10% position §14.25 already established. `FFmpegMediaDecoder` replaces its
  hardcoded `duration / 10` seek with `duration * fractionForIndex(previewIndex)`;
  `VipsImageDecoder` accepts and ignores the parameter; the `MediaDecoder` dispatch facade passes
  it through.
* **`ThumbnailCache::Key`** gains `int previewIndex = 0`, folded into the existing XXH64 filename
  hash — each of a video's 5 frames becomes its own cache entry, at the same
  path+size+mtime+dimensions otherwise. `CachingMediaDecoder` threads it through to the `Key` and
  the inner decode call on a miss.
* **Cache size, widened for the new per-video 5x entry count** (images are unaffected — still one
  entry each): `ThumbnailCache`'s default on-disk cap, 512MB → 1536MB; its in-memory LRU
  (`kMemoryLruCapacity`), 20 → 60 entries, so scrubbing back and forth across several recently-
  hovered videos stays a memory hit instead of thrashing back out to disk.
* **`MediaThumbnailData`** (`src/adapters/viewmodels/media`) gains `std::vector<QImage>
  previewFrames` — empty for an image row (`image` stays the only frame, unchanged); `kCount`
  entries in time order for a video row (`previewFrames[0] == image`, decoded once and reused, not
  duplicated). **`MediaThumbnailLoadingWorker::thumbnailsFor`** branches on
  `MediaExtensions::isVideoExtension`: an image entry keeps its single existing decode call; a
  video entry makes `kCount` calls (`previewIndex` 0..4), backfilling any one failed slot from its
  nearest successfully-decoded neighbor so scrubbing never hits a blank frame, and omitting the
  entry entirely only if every slot fails (same "placeholder keeps showing" posture as today).
* **`MediaGridTileDelegate`** gains hover state (`QPersistentModelIndex` + the hovered band index,
  set/read via `setHoveredPreview`/`hoveredIndex`/`hoveredPreviewIndex`) and paints
  `previewFrames[hoveredPreviewIndex]` instead of `image` only for the currently-hovered video row
  with a complete frame set — every other row (image, non-hovered, still-decoding) paints exactly
  as §14.62 already does. `thumbnailRectFor(option)` is refactored into a cell-rect-only overload
  (`thumbnailRectForCellRect`) so `FileBrowserView` can compute the same thumbnail sub-rect without
  building a full `QStyleOptionViewItem`.
* **`FileBrowserView`** enables `setMouseTracking(true)` on the grid's `QListView` viewport only in
  `DisplayMode::MediaViewerGrid`, and extends its existing viewport `eventFilter()` with a
  `MouseMove`/`Leave` branch (observes, doesn't consume) that resolves the index under the cursor,
  skips anything that isn't a video row (`FilePathRole` + `MediaExtensions::isVideoExtension`, same
  check the delegate itself uses), maps the cursor's `x` within
  `thumbnailRectForCellRect(visualRect(index), iconSize())` to a 0-based band, and — only on an
  actual change — updates the delegate's hover state and issues a targeted `update()` on the
  previous and new hovered cells rather than the whole viewport.
* **Not built:** lazy/on-demand decoding of the 4 non-default frames (all 5 are decoded upfront
  during the existing chunked grid-population pass, same trigger point as today's single poster
  frame) — a video-heavy folder's grid now does ~5x the FFmpeg decode work to fully populate
  (images are unaffected); left as a future optimization (e.g. decode frame 0 eagerly, defer 1–4 to
  a lower-priority pass) if this proves too slow in practice. No change to the Preview panel's
  single-frame video preview, and no persistence of hover state across selection/navigation (purely
  ephemeral, cleared on mouse leave).
* **Testing**: `MockMediaDecoder`/`MediaProcessingUseCaseTest`/`FilePreviewUseCaseTest` updated to
  the new 4-argument `generateThumbnail` signature, plus a case asserting an out-of-range
  `previewIndex` fails `InvalidArgument`; `FFmpegMediaDecoderTest` gains a case covering multiple
  `previewIndex` values; `CachingMediaDecoderTest`/`ThumbnailCacheTest` gain cases asserting two
  different `previewIndex` values for the same file produce two independently cacheable entries.
  No new coverage for the `MediaThumbnailLoadingWorker`/`MediaGridTileDelegate`/`FileBrowserView`
  hover wiring, matching §14.61/§14.62's own precedent for this layer (no existing tests either) —
  manual smoke test only.

### 14.65 Media Viewer Mode: Loading Progress Notification

§14.61's "Not built" bullet flagged this gap directly: entering Media Viewer mode runs a recursive
scan (§14.38) followed by chunked thumbnail decoding (§14.62, ×5 per video since §14.64) with no
feedback beyond a static "Loading…" label bound only to the scan phase — the slower decode phase
that follows gives no indication at all. This wires entering Media Viewer mode into the same generic
progress-bar notification mechanism §14.47 built for copy/move
(`NotificationCenter::createProgressNotification`/`updateProgressBar`/`terminateProgressNotification`,
`src/adapters/notifications/NotificationCenter.h`) instead of inventing a second one. Entirely
Adapters/UI — no new Port, no Domain/Application/schema change, no change to `NotificationCenter`/
`NotificationOverlayWidget`/`ProgressNotificationWidget` itself (the existing single-bar/cancellable/
three-text-row API already covers it).

* **`TabViewModel`** is the owner/driver, exactly like `FileOperationsController` owns copy's progress
  id — it already owns the per-tab `m_mediaViewerGeneration` counter and both result-fetching call
  sites. Gains a `NotificationCenter&` constructor dependency (threaded through
  `WorkspacePaneViewModel`'s constructor and `CompositionRoot`'s `createTabViewModel()`/
  `createWorkspacePaneViewModel()` factories, alongside `WorkspaceController`'s existing
  `m_notificationCenter`) plus a `NotificationCenter::ProgressId m_mediaViewerProgressId` (0 = none
  active) and running `m_mediaViewerThumbnailsTotal`/`m_mediaViewerThumbnailsDone` counters.
* **Two phases**, matching copy's own indeterminate-then-determinate shape: (1) *Scanning* —
  indeterminate (0/0) while `AdvancedSearchService::requestRecursiveScan`/`requestTagFirstCandidates`
  runs; the recursive-scan path now passes the `onProgress` callback `requestRecursiveScan` already
  supported but `runMediaViewerQuery()` left unused, updating the bar's text with a running
  files-found count (the tag-first path has no equivalent hook, and keeps static text — its DB-driven
  lookup is comparatively fast). (2) *Loading thumbnails* — determinate `0..N`,
  `N = m_mediaViewerCandidates.size()` (the same full, kind-unfiltered set
  `requestMediaViewerResultThumbnails` already fetches); each `onBatch` chunk from
  `MediaThumbnailLoadingService` advances the bar by `batch.size()`, terminating the notification once
  `done == total`. Only the initial fresh-candidate load drives this — `applyMediaViewerFilter`'s
  re-filter calls (kind toggle/name-query keystrokes) reuse the same generation but find
  `m_mediaViewerProgressId` already back to `0` by then, so they never resurrect the notification for
  thumbnails that are already cached.
* **Cancellable** (matching copy): `NotificationCenter::progressCancelRequested(id)` is wired, when
  `id` matches, to call `exitMediaViewer()`. Same "discard, not abort" posture Architecture.md §5
  already documents for every background worker in this codebase except `FileTransferService` — no
  mid-flight interrupt of in-flight decode chunks; `exitMediaViewer()`'s existing generation bump just
  makes already-in-flight batches land and get silently discarded, same as manually toggling Media
  Viewer mode off mid-load already did before this feature.
* **Not built**: no progress reporting for `requestMediaViewerResultIcons` (generic shell icons via
  `IconLoadingService` — fast enough today not to need it); no true abort of in-flight FFmpeg decodes
  on cancel (see above).
* **Testing**: no new GTest coverage — `TabViewModel` has no existing tests either (§11 UI/ViewModel-
  glue carve-out, same precedent as §14.61/§14.62/§14.64); manual smoke test only.

### 14.66 Media Viewer Mode: Kind-Scoped Loading + Filter-Priority Decoding

§14.62/§14.65 had Media Viewer mode decode thumbnails for **both** Images and Videos the moment it's
entered, even though the Images/Videos dropdown only ever shows one kind at a time — wasting decode
time (FFmpeg video decode is the expensive case, ×5 frames/video since §14.64) on a kind the user may
never look at, and giving no priority to whatever the current name filter actually matches over a
large background sweep already mid-flight. Three fixes, entirely Adapters/UI — no new Port, no
Domain/Application/schema change.

* **Only the selected kind is decoded eagerly.** `TabViewModel::onMediaViewerCandidatesReady` no
  longer computes a both-kinds `mediaCandidates` set; it calls a new `requestKindSweep(generation,
  kind)` helper for just `m_mediaViewerKind` instead. `requestKindSweep` filters
  `m_mediaViewerCandidates` by `kind` alone (ignoring the name filter, so clearing it later never
  waits on a fresh decode pass), sizes/owns the "Loading Media Viewer" progress notification exactly
  as §14.65 did (just for one kind's count instead of both), and dispatches
  `requestMediaViewerResultIcons`/`requestMediaViewerResultThumbnails` for that kind-filtered set —
  also fixing `requestMediaViewerResultIcons`'s previous "every file in the tree" call site, since
  icons for non-media files were never shown in the grid anyway.
* **Switching kind triggers a real load.** Two per-generation bools,
  `m_mediaViewerImageSweepRequested`/`m_mediaViewerVideoSweepRequested` (reset in
  `onMediaViewerCandidatesReady` whenever `m_mediaViewerCandidates` is replaced by a fresh fetch),
  track which kind(s) have already had `requestKindSweep` dispatched this generation.
  `setMediaViewerKind` calls `requestKindSweep(m_mediaViewerGeneration, kind)` right after its
  existing `applyMediaViewerFilter(false)` re-filter; `requestKindSweep` no-ops if that kind's sweep
  already ran, so switching to a never-seen kind now visibly decodes it (and shows the progress bar
  for that switch, reusing the generic notification in place if one is already open, or opening a
  fresh one if the previous kind's sweep had already terminated it).
* **Filter-matching items jump the queue.** `MediaThumbnailLoadingService::requestThumbnails` gains a
  `Priority` parameter (`High`/`Low`), replacing its old flat "post every chunk via
  `QMetaObject::invokeMethod` immediately" dispatch with an internal mutex-guarded
  `std::deque<PendingChunk>`, pumped one chunk at a time by the same single worker `QThread` (no
  pool, same reasoning as before — everything shares one `MediaProcessingUseCase`/
  `CachingMediaDecoder` instance). A `High` dispatch first purges any of that same context's
  still-queued (not yet started) `High` chunks (a superseded filter value, e.g. the previous
  keystroke's matches), then pushes its new chunks to the deque's front; `Low` chunks are always
  left alone and simply appended to the back. The per-context "active root" staleness check moved
  from enqueue time to pop time (checked right before a chunk is actually handed to the worker).
  `TabViewModel::applyMediaViewerFilter` dispatches its kind+name-filtered subset at `High` (it's on
  screen right now); `requestKindSweep`'s whole-kind background sweep dispatches at `Low`.
  `MediaThumbnailLoadingWorker` itself is unchanged — only the service's dispatch/queueing wrapper
  changed.
* **Progress-counting correctness**: since a `High` and a `Low` dispatch can now legitimately
  overlap (a filter-matching item is both in the priority subset and part of the kind's full sweep),
  raw batch-size summation would double-count it against `m_mediaViewerThumbnailsTotal`. Replaced
  with a de-duplicated `QSet<QString> m_mediaViewerCountedPaths` (reset alongside
  `m_mediaViewerThumbnailsTotal`/`Done` whenever a sweep establishes a new total): each `onBatch`
  only advances `done` for a path not already in that set. `requestMediaViewerResultThumbnails` lost
  its original `reportProgress` bool, which gated counting to `requestKindSweep`'s own `Low` dispatch
  only — with no active name filter, `applyMediaViewerFilter`'s `High` dispatch covers most or all of
  the kind and, being the one that jumps the queue, is also the one that actually finishes decoding
  first, so the bar sat stuck at 0 (visibly so on a large folder) while the grid was already filling
  in, only catching up once the `Low` sweep's own redundant (cache-hit) decodes of the same paths
  landed behind it. Every delivered batch now counts whenever `m_mediaViewerProgressId != 0`,
  regardless of which dispatch delivered it; the path dedup already guarantees neither is counted
  twice.
* **Not built**: no change to `IconLoadingService` (icons are cheap/fast shell lookups, not the
  bottleneck this is about); no true cancellation of an in-flight FFmpeg decode already running when
  superseded — same "discard, not abort" posture §14.65 established, only **not-yet-started** chunks
  are purged/reordered; no debounce on name-filter keystrokes (purging is a cheap deque scan, not
  decode work); no persistence of sweep-requested state across restarts or re-entering Media Viewer
  mode (reset every fresh candidate fetch, same as every other Media Viewer transient state).
* **Testing**: no new GTest coverage, matching every existing Media Viewer entry in this decision log
  (§14.61/§14.62/§14.64/§14.65 all note this layer stays under the §11 UI/ViewModel-glue carve-out);
  manual smoke test only.

### 14.67 Simple Built-In Video Player (Widgets, not QML)

Specification.md §2.4 and the original Architecture.md sketch called for a full-window QML viewer
(`MediaPreviewViewModel`, via `QQuickWidget`) for video playback, but no QML file was ever added to
the project -- every other media feature (poster frames, grid hover previews, §14.26/§14.58/
§14.64) shipped in plain Qt Widgets instead. Rather than introduce the project's first QML surface
and `Quick`/`Qml`/`QQuickWidget` CMake plumbing for what the user wants kept simple, the player is
plain Qt Widgets + Qt Multimedia (`QMediaPlayer`/`QVideoWidget`/`QAudioOutput`), consistent with the
rest of the codebase. `qtmultimedia`'s vcpkg port already default-builds the `widgets` feature and
the `ffmpeg` decode backend, so no `vcpkg.json` change was needed -- only `find_package(Qt6 ...
Multimedia MultimediaWidgets)` at the top level and linking those two modules into
`explorer_ui_widgets`.

* **No new Port.** `QMediaPlayer` loads the video file directly from `FilePreview::path()`; there is
  no business logic to express as a use case, so `VideoPlayerWidget` talks to Qt Multimedia
  directly with no `IVideoPlayer`-style abstraction -- same posture as `TerminalWidget`/
  `WindowsConPtyProcess` (Architecture.md §9/§10): a pure OS/Qt integration with no second
  implementation ever in scope, so an interface would buy nothing.
* **Standalone component** (`src/ui/widgets/video/VideoPlayerWidget`), not nested under `preview/`:
  the user explicitly wants it reusable later (e.g. Media Viewer grid playback, a future
  full-window viewer) without having to move code out from under an unrelated feature's folder
  first. `PreviewPanelWidget` is just its first consumer -- the same relationship `preview/`
  already has with `SyntaxHighlightEngine`/`HighlightThemeViewModel`.
* **UI shape**: `QVideoWidget` on top (stretches); a controls row below with a play/pause toggle
  `QToolButton` (`SP_MediaPlay`/`SP_MediaPause`), a separate Stop `QToolButton`
  (`SP_MediaStop`, distinct from pause per the user's request), and a `mm:ss / mm:ss` time label;
  a bottom row with the seek `QSlider` (stretch), a mute-toggle `QToolButton`
  (`SP_MediaVolume`/`SP_MediaVolumeMuted`), and a volume `QSlider` (0-100, mapped to
  `QAudioOutput::setVolume()`'s 0.0-1.0). All icons are Qt standard icons -- no new icon assets.
* **Seek-drag handling**: while the seek slider is pressed (`sliderPressed` through
  `sliderReleased`), incoming `positionChanged` updates are suppressed so playback doesn't fight the
  drag; `sliderMoved` live-updates the time label; `sliderReleased` commits via
  `player->setPosition()`.
* **Public API**: `load(path)` sets the source (`QUrl::fromLocalFile(QString::fromStdWString(
  path.wstring()))` -- never `.string()`/`.generic_string()`, per CLAUDE.md's non-ASCII-path rule)
  and leaves playback **paused** (first frame visible, no autoplay/audio burst on selection).
  `release()` stops playback and clears the source; called from every point the widget can stop
  being the visible content -- `PreviewPanelWidget::hideEvent`, `onPreviewLoading`,
  `onPreviewFailed`, `onPreviewCleared` (unconditionally, since a no-op release is harmless), and
  whenever `onPreviewReady` resolves to a non-Video kind or the mode combo switches away from the
  player -- so playback/audio never continues once the Preview tab is hidden, the selection changes,
  or a newer preview generation supersedes it.
* **Preview tab integration**: `PreviewPanelWidget` gains a `QComboBox` ("Static image" / "Video
  player") shown only while the current preview's kind is `Video`, plus `VideoPlayerWidget` as a
  new `m_stack` page. The last-seen Video `FilePreview` is kept as a member
  (`std::optional<FilePreview>`) so switching the combo re-renders through the newly selected page
  without re-asking `FilePreviewViewModel`; "Static image" renders through the existing
  `MediaPreviewWidget::setPreview()` (same poster-frame path image/video already shared), "Video
  player" calls `VideoPlayerWidget::load()`. The mode choice is a plain member, not persisted via
  `AppConfig`/`QSettings` -- not requested, and avoids config/schema churn for a "simple" player.
* **Deployment**: the FFmpeg and Windows Media Foundation playback-backend plugin DLLs
  (`ffmpegmediaplugin.dll`, `windowsmediaplugin.dll`, targets `Qt6::QFFmpegMediaPlugin`/
  `Qt6::QWindowsMediaPlugin`) are loaded by Qt Multimedia at runtime from a `multimedia/` subfolder
  next to the executable -- vcpkg's applocal DLL copy doesn't reach plugin-loaded DLLs, so
  `src/app/CMakeLists.txt` deploys them explicitly, same pattern as the existing `platforms/`/
  `sqldrivers/`/`imageformats/` post-build steps. No separate Windows audio-backend plugin exists
  (WASAPI is compiled in).
* **Not built** (Specification.md §2.4 lists these; not requested now): fullscreen toggle,
  aspect-ratio adjustments, loop playback; any playback in the Media Viewer grid/gallery mode
  (stays poster + hover-preview only, §14.62/§14.64/§14.66); persisting the mode choice across
  sessions; any change to the `IMediaDecoder`/`FFmpegMediaDecoder`/thumbnail-cache pipeline, which
  keeps serving the poster frame and grid hover previews exactly as before.
* **Testing**: no new GTest coverage -- pure UI-layer addition, no Domain/Application/Adapter
  change, and UI stays uncovered by CTest in v1 (Architecture.md §11); manual smoke test only.

### 14.68 Media Viewer Mode: Sort + View Controls

Media Viewer mode's grid (§14.61) had no sort control at all -- `TabViewModel::applyMediaViewerFilter`
hard-coded `FileNavigationUseCase::sortBy(filtered, SortCriterion::Name, true)` -- and no "view"
concept beyond the single locked `FileBrowserView::DisplayMode::MediaViewerGrid`. This adds three
dropdowns (sort-by, sort-direction, view), grouped in one container widget, to the per-pane
navigation toolbar (`WorkspacePaneWidget`'s `QToolBar`) -- the toolbar that §14.61 disables
entirely while Media Viewer mode is active. The new container sits right after the address bar
and is the one piece of that toolbar that stays visible/enabled while every other control (back/
forward/up/refresh/address bar/view-mode button) keeps being disabled exactly as before.

* **New enums** (`src/adapters/viewmodels/media/MediaViewerSortCriterion.h`,
  `MediaViewerViewMode.h`, same Qt-free posture as `MediaViewerKind.h`): `MediaViewerSortCriterion
  { Name, Duration, Size }` and `MediaViewerViewMode { Thumbnails }`. A separate sort enum rather
  than extending the existing (Application-layer) `SortCriterion` -- `Duration` is meaningless for
  plain file browsing/directories, so it stays scoped to Media Viewer. `MediaViewerViewMode` has a
  single enumerator today and is a deliberate stub for future view modes the user asked to be able
  to add later (e.g. a list view) -- `setMediaViewerViewMode` only stores state and emits a signal
  for the new combo to reflect; the grid itself stays locked to `DisplayMode::MediaViewerGrid`
  regardless.
* **State lives on `TabViewModel`** (`m_mediaViewerSortCriterion`, `m_mediaViewerSortAscending`,
  `m_mediaViewerViewMode`, defaulting to Name/ascending/Thumbnails), per-tab like `m_mediaViewerKind`
  -- UI only dispatches to it, no business logic in the widget.
* **Duration sort reuses §14.62's thumbnail cache.** `FileNode`/the recursive-scan candidates never
  carry a duration (it's only known once `MediaProcessingUseCase::generateThumbnail()` decodes a
  video), so Duration-sort compares against `FileListModel::m_mediaThumbnailsByPath` via a new
  `mediaThumbnailDuration(const QString& path)` accessor instead of adding a new field to `FileNode`.
  An item whose thumbnail hasn't decoded yet always sorts after every item with a known duration
  (regardless of ascending/descending) rather than being treated as a 0ms duration. Because that
  cache fills in progressively, `TabViewModel::requestMediaViewerResultThumbnails`'s batch callback
  re-sorts (`reapplyMediaViewerSort()`) whenever the active criterion is Duration, so the grid
  reorders live as videos finish decoding instead of only once at load.
* **`reapplyMediaViewerSort()`** is a cheap re-order only -- it re-filters the already-fetched
  `m_mediaViewerCandidates` by kind/name (unchanged candidates, no new I/O) and re-sorts, then goes
  through `FileListModel::refilterEntries()` so it never touches icon/thumbnail requests; changing
  sort order never changes which paths need decoding.
* **UI**: `MediaViewerSortViewBar` (`src/ui/widgets/navigation/`), a plain `QWidget` (same shape as
  `AddressBarWidget`, no theme notifier) holding three tooltipped `QComboBox`es in a `QHBoxLayout`,
  added to `WorkspacePaneWidget::createToolBar()` right after `AddressBarWidget` and hidden by
  default; `updateToolBarEnabledForMediaViewer` shows/enables it exactly when Media Viewer mode is
  active (mirroring how it disables everything else), and the pane's tab-bind/unbind lifecycle
  resyncs it from (and forwards edits to) whichever `TabViewModel` is currently bound, the same
  pattern already used for `viewModeChanged`/`mediaViewerModeChanged`.
* **No new Port, no schema change.** Purely Adapters(ViewModel) + UI, consistent with every other
  Media Viewer entry.
* **Testing**: no new GTest coverage, same precedent as §14.61/§14.62/§14.67 (this layer sits under
  the Architecture.md §11 UI/ViewModel-glue carve-out); manual smoke test only.

### 14.69 Media Viewer Mode: Filmstrip + Details View Modes

§14.68 added the view combo but left `MediaViewerViewMode` a deliberate single-enumerator stub —
nothing downstream of `TabViewModel::setMediaViewerViewMode` reconfigured the grid, and
`FileBrowserView::setViewMode` unconditionally locked `DisplayMode::MediaViewerGrid` to the one
icon-grid layout set up at construction. This adds the two promised view modes: **Filmstrip** (one
full-width row per item, every decoded preview frame shown side-by-side, name underneath) and
**Details** (one full-width row per item: hover-scrubbable preview on the left, name/length/size/
resolution/tags on the right). No new Port, no schema change — purely Adapters(ViewModel) + UI,
consistent with every other Media Viewer entry.

* **`MediaViewerViewMode`** gains `Filmstrip`/`Details` alongside `Thumbnails`. `MediaViewerSortViewBar`'s
  view combo gains the two matching items; its `currentIndexChanged` lambda and `setViewMode()`'s
  switch extend to a 3-way mapping (same shape as the sort-criterion combo's existing 3-way one).
* **Both new modes render through the existing `m_listView`**, not a new stack page or the
  `QTreeView` — `QListView::ListMode` + `Flow::TopToBottom` + `setWrapping(false)` (the one
  existing mode that sets `Wrapping(true)`, `ViewMode::List`, deliberately doesn't: it wants
  Explorer's short multi-column list) gives single-column rows that stretch to the full viewport
  width automatically, like a plain `QListWidget` — no gridSize/resize tracking needed.
* **`FileBrowserView`** gains `setMediaViewerViewMode(MediaViewerViewMode)` (no-op outside
  `DisplayMode::MediaViewerGrid`), swapping `m_listView`'s installed delegate and
  flow/wrapping/gridSize/iconSize per mode: `Thumbnails` keeps today's IconMode/grid setup
  (`m_mediaGridDelegate`); `Filmstrip`/`Details` both use the ListMode/no-wrap config above, with
  `m_mediaFilmstripDelegate`/`m_mediaDetailsDelegate` respectively. The constructor's one-time
  `MediaViewerGrid` setup now ends by calling `setMediaViewerViewMode(MediaViewerViewMode::Thumbnails)`
  instead of inlining that branch's configuration twice.
* **`WorkspacePaneWidget::addPageForTab`** connects `TabViewModel::mediaViewerViewModeChanged` to
  the per-tab `mediaViewerBrowserView`'s new `setMediaViewerViewMode` slot, and applies the tab's
  current value once at page-construction time — previously that signal only reached the toolbar
  combo (`MediaViewerSortViewBar::setViewMode`, resync only), never the grid itself, which is why
  the combo had no visible effect before this.
* **`MediaFilmstripDelegate`** (`src/ui/widgets/media/`, new): `QStyledItemDelegate`, fixed-height
  `sizeHint()` (row width comes from the ListView config above, not the delegate). Reuses
  §14.64's `MediaThumbnailData::previewFrames` with no new decode work: a video with a full
  `MediaPreviewFrames::kCount`-frame set paints all of them side-by-side (thin 1px dividers
  between bands); an image, or a video not yet fully decoded, paints its single `image` stretched
  across the whole row. Filename caption centered below a thin divider line, same font/
  foreground-role handling as `MediaGridTileDelegate::paint`; dashed selection outline on
  `State_Selected`, matching every other delegate here.
* **`MediaDetailsDelegate`** (`src/ui/widgets/media/`, new): `QStyledItemDelegate` with hover
  state mirroring `MediaGridTileDelegate`'s shape (`setHoveredPreview`/`hoveredIndex`/
  `hoveredPreviewIndex`, a `thumbnailRectForCellRect` static over a small fixed-size square
  anchored left, vertically centered) — Qt's per-cell single-delegate model means a literal
  shared-delegate-instance reuse isn't possible for two independently-laid-out regions of one
  cell, so this mirrors the API shape and shares the actual pixel-painting code instead (see
  `MediaThumbnailPainting` below) rather than duplicating it a third time. Right side: bold/
  larger-point name; a `duration | size | WxH` line (duration segment and its separator omitted
  for an image row, same `MediaExtensions::isImageExtension` branch `MediaGridTileDelegate`
  already uses; resolution omitted until known); a tag-pill row via `TagPillRowPainter` (below)
  against `FileListModel::TagsRole`.
* **`MediaThumbnailPainting`** (`src/ui/widgets/media/`, new): `paintThumbnail` (aspect-fit
  letterbox)/`paintPlaceholderIcon`/`durationLabel`, lifted unchanged out of
  `MediaGridTileDelegate.cpp`'s anonymous namespace so `MediaFilmstripDelegate`/
  `MediaDetailsDelegate` can reuse the same non-trivial pixel-painting code instead of a third
  copy — unlike the small layout-rect math this codebase deliberately keeps duplicated per
  delegate (§14.62's own comment), real image-scaling/formatting logic earns a shared helper once
  3 call sites need it. `MediaGridTileDelegate` itself is updated to call these instead of its own
  copies; its behavior is unchanged.
* **`TagPillRowPainter`** (`src/ui/widgets/tags/`, new): `paintTagPillRow`/`tagPillRowHeight`,
  lifted out of `TagsCellDelegate.cpp`'s anonymous namespace (the pill-paint loop, including the
  trailing "+N" overflow pill) for the same reason — now shared between `TagsCellDelegate` and
  `MediaDetailsDelegate`. `TagsCellDelegate`'s own behavior is unchanged.
* **`updateMediaViewerHover()`** (FileBrowserView's existing §14.64 hover-scrub handler) becomes
  mode-aware: a no-op while `Filmstrip` is active (every frame is already visible at once, no
  single-frame hover concept); while `Details` is active, resolves the thumbnail sub-rect via
  `MediaDetailsDelegate::thumbnailRectForCellRect` instead of `MediaGridTileDelegate`'s, and
  updates `m_mediaDetailsDelegate`'s hover state instead. The "skip non-video rows"/"repaint only
  on an actual change" logic is otherwise unchanged.
* **Tags weren't fetched for the Media Viewer results model at all before this** —
  `TabViewModel::requestFileTags` was only ever called for the normal-browsing `m_fileListModel`,
  never for `m_mediaViewerResultsModel`, since nothing needed them before Details' tag row.
  `TabViewModel` gains `requestMediaViewerResultTags(generation, candidates)` (mirrors
  `requestSearchResultTags`/`requestAdvancedSearchResultTags`'s existing shape), called from
  `applyMediaViewerFilter()` alongside the existing icon/thumbnail requests and from the
  `TagChangeNotifier::tagsChanged` handler alongside the search/advanced-search tag refreshes —
  feeding `m_mediaViewerResultsModel->setFileTags(...)`.
* **Not built**: persisting the view-mode choice across restarts (same as §14.68's existing
  Thumbnails-only posture); hover-scrub for Filmstrip (see above); any change to the normal
  file-browser's own `ViewMode::Details`/`ViewMode::List` (a separate enum/concept from
  `MediaViewerViewMode`, untouched here).
* **Testing**: no new GTest coverage, same precedent as §14.61/§14.62/§14.64/§14.67/§14.68 (this
  layer sits under the Architecture.md §11 UI/ViewModel-glue carve-out); manual smoke test only.

### 14.70 Tag Panel: Keyboard-Driven Autocomplete (Tab-complete, Enter-to-assign-or-create)

The tag panel's search box (`TagPanelWidget`'s `m_searchEdit`, §14.9) was a bare `QLineEdit` —
assigning a tag required a mouse click on a chip's "+" (or the "Create tag" button). This adds a
fully keyboard-driven path on top of the same `TagListViewModel::searchResults()`/
`canCreateTagFromQuery()`/`createAndAddTagFromQuery()`/`addTagToSelection()` machinery §14.9 already
built — no Domain/Application/Port/schema change, purely UI plus one small ViewModel addition, same
posture as §14.46/§14.69.

* **`TagSearchEdit`** (`src/ui/widgets/tags/`, new) replaces the plain `QLineEdit`: a popup/Tab/
  ghost-completion widget mirroring `AddressBarWidget` (§14.21) — frameless `QListWidget` popup
  under the edit, inline ghost-suffix completion, Up/Down moves the popup highlight, Escape
  dismisses, `bool event(QEvent*)` overridden (not just `keyPressEvent`) to claim Tab before Qt's
  focus-chain traversal claims it first. Unlike the address bar it needs no debounce timer or async
  suggestion request: `TagPanelWidget` pushes the already-computed, already-ranked
  `TagListViewModel::searchResults()` straight into `setSuggestions()` on every
  `searchResultsChanged` (same synchronous same-thread signal chain as the existing chip rebuild),
  since `searchResults()` is itself driven by the same `textChanged → setSearchQuery` keystroke that
  triggered the update. Because `searchResults()` can rank a substring-only match first (not just
  prefix matches, unlike the address bar's folder list), the ghost suffix only appears when the top
  suggestion `startsWith()` the typed text; otherwise the popup still shows with no ghost. Tab's
  `acceptPendingCompletion()` falls back through three cases — an arrow-key-highlighted row, a
  pending ghost suffix, or (no highlight/ghost at all) the first ranked suggestion — before giving up
  and letting Tab fall through to normal focus-chain traversal. Clicking a popup row with the mouse
  completes the text exactly like Tab; it does not assign anything on its own, keeping "assign" an
  exclusively Enter/`+`-button action.
* **Deliberate divergence from `AddressBarWidget`'s Enter handling**: the address bar's Enter
  accepts whatever popup row is highlighted. Here Enter is left uncaptured by `TagSearchEdit`
  entirely — it falls straight through to `QLineEdit`'s default handling (just the inherited
  `returnPressed()` signal) — because Enter's job is unrelated to the popup: it always acts on the
  literal typed text, never on what's highlighted.
* **`TagListViewModel::acceptSearchQuery()`** (new public slot): the Enter-key commit. Empty
  (trimmed) query is a no-op. Otherwise: `m_canCreateTagFromQuery` true → calls the existing
  `createAndAddTagFromQuery()` (create + assign, same as the "Create tag" button); false → an exact
  case-insensitive name match is scanned for in `m_searchResults` and, if found,
  `addTagToSelection()`'d (same as the "+" button). Because `m_searchResults` already excludes tags
  already on the resolved target (§14.9), an exact match that's already assigned simply isn't found
  there, so that case falls through as a harmless no-op with no extra special-casing needed.
* **`TagPanelWidget`** wires the new pieces: `TagListViewModel::searchResultsChanged` additionally
  feeds `TagSearchEdit::setSuggestions()` (tag names only, same rank order used for the chip row);
  `TagSearchEdit`'s inherited `returnPressed()` calls `acceptSearchQuery()` then clears the edit.
  Clearing on every Enter (success, no-op, or failure alike) is new behavior — the "Create tag"
  button never cleared the box — needed so rapid, successive keyboard-only tagging doesn't get stuck
  behind stale text.
* **Not built**: no inline color swatch on popup rows (plain tag-name text only); no change to the
  existing mouse-driven chip row, which keeps working exactly as before.
* **Testing**: no new GTest coverage — `TagListViewModel` is untested per Architecture.md §11's
  existing convention (same as `WorkspaceController`), and `TagSearchEdit`/`TagPanelWidget` are UI
  widgets outside CTest's v1 scope; manual smoke test only.
