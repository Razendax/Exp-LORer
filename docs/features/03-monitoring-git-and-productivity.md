# Architecture Document — Feature Decision Log: Monitoring, Git & Productivity Features

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** (§14.44 is the one exception: a small,
additive set of new Domain name-matching helpers and an Application-layer options struct, not a
redesign of either layer's existing shape) — and is recorded as a decision log per feature, not an
implementation walkthrough; see git history and the code's own comments (many cite these subsection
numbers directly) for the detail.

Covers §14.34–§14.45. Preceded by
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
  barCount)` returns an id; client code asynchronously updates a bar's current/max/text or
  terminates the whole notification via that id. **Not wired to any real operation yet** — copy/
  move stay synchronous with no progress UI (§5's existing accepted gap) — only the generic
  mechanism exists, for a future feature to call into.
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
  `ProgressNotificationWidget` (title + N labeled `QProgressBar` rows, no manual close — ends only
  via `terminateProgressNotification`); `NotificationOverlayWidget` (a plain child of `MainWindow`,
  always raised, anchored bottom-right via a `resizeEvent` override, stacking the cards above in a
  bottom-aligned layout). `SettingsDialog` gains a `General → Notifications` category: a fixed
  3-row table (Type / Enabled checkbox / Timeout spinbox in seconds), immediate-apply like every
  other Settings page — no Add/Remove, unlike the Colors page's editable rule list, since the rows
  are fixed. `UiColors.h` gains `kNotificationInfoBlue`/`kNotificationWarningYellow`/
  `kNotificationErrorRed`.
* **Not built:** progress-bar notifications wired into a real operation; a cap on simultaneously
  visible notifications/overflow scrolling (unbounded stacking accepted); sound; click-to-action;
  a history/log of dismissed popups; persisting in-flight notifications across restart (only the
  3-row settings persist). No automated coverage for `NotificationCenter` or any widget above
  beyond the existing §11 UI/ViewModel-glue carve-out; `NotificationSettings`'s plain logic and
  `NotificationSettingsStore`'s JSON round-trip get ordinary GTest coverage under `tests/adapters`.

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
* **Per-tab visibility, not session state.** Unlike column *widths* (one shared
  `AppConfig::detailsColumnWidths` array applied to every tab), column *visibility* is genuinely
  per-tab, so it is **not** added to `TabConfig`/`AppConfig` — it lives directly on each
  `FileBrowserView`'s `QHeaderView` (`setSectionHidden`/`isSectionHidden`), exposed via
  `FileBrowserView::setColumnVisible`/`isColumnVisible` and a new
  `columnVisibilityChanged(Column, bool)` signal. A tab's three `FileBrowserView` instances (normal
  browsing, quick-search results, advanced-search pane, §14.19/§14.18) are kept in sync by that
  signal, the same "seed all three the same way" posture their column widths already have. Right-
  click on `QTreeView::header()` (`Qt::CustomContextMenu`) opens a `QMenu` of checkable per-column
  actions (Name excluded); this is purely a UI-widget-layer addition, no ViewModel/Application
  change, and — like the rest of the UI layer — has no CTest coverage (§11).
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
  `AdvancedSearchWorker` and `FileTagsService`/`FileTagsWorker`. `requestScan(tabId, root)` is queued
  to the worker thread; `scanFinished(tabId, Result<vector<DiskUsageNode>>)` marshals back to the UI
  thread, `tabId` letting a stale result from an abandoned/replaced tab be discarded on arrival. No
  cancellation in v1 — the `FindFirstFileW` walk isn't interruptible mid-call, same accepted posture
  as Advanced Search's recursive scan (§5). Only one scan in flight per tab; both buttons disable for
  the duration.
* **ViewModel/UI:** `FolderSizeViewModel` (`adapters/viewmodels`, one per tab, same lazy-start
  lifetime as `TerminalWidget`) tracks the tab's current path (read-only via
  `TabViewModel::currentPathChanged`) and drives `DiskUsageService`. `PieChartWidget`
  (`src/ui/widgets`, new, plain `QPainter` — no QtCharts/new vcpkg dependency for one chart) paints
  up to 10 wedges with name/percentage/size labels and emits `segmentClicked(path)` on hit-test;
  `FolderSizeWidget` hosts it plus the two buttons (text flips to "Scanning..." and both disable
  while a scan is in flight) and wires `segmentClicked` to `TabViewModel::navigateTo`. Registered via
  a second `bottomPanel->addPanelTab(tr("Folder Size"), folderSizeWidget)` call in
  `WorkspacePaneWidget::addPageForTab`, right after the existing Terminal registration —
  `BottomPanelWidget::addPanelTab`'s panel-tab button `objectName` becomes index-derived (it was
  hardcoded for the single pre-existing Terminal caller) so each panel tab's button keeps a distinct,
  stable id (§11's UI-Automation-id convention).
* **Not built:** Linux scanner (Port exists; adapter returns "not supported"); a progress bar (button
  text only); scan cancellation; auto re-scan on navigation, live directory-change events, or a
  timer; persisting the bottom panel's expanded/collapsed state (unchanged from §14.24); any
  integration with tagging/Preview/search.
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
* **Not built:** persisting the panel's state/width; files, multi-select or drag-and-drop in the
  tree; a background ("New Folder") menu on empty tree space; async listing. (A second left-panel
  tab, "Search," is added by §14.42.)

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
