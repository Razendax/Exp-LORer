# Architecture Document — Feature Decision Log: Search, Persistence & File Tools

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign**, aside from a handful of features below
that add their own small Domain/Application pieces (called out inline where relevant) — and is
recorded as a decision log per feature, not an implementation walkthrough; see git history and the
code's own comments (many cite these subsection numbers directly) for the detail.

Covers §14.44–§14.56. Preceded by
[01-workspace-navigation-and-search.md](01-workspace-navigation-and-search.md) (§14.1–§14.26),
[02-preview-decoration-and-status.md](02-preview-decoration-and-status.md) (§14.27–§14.33), and
[03-monitoring-git-and-productivity.md](03-monitoring-git-and-productivity.md) (§14.34–§14.43);
continued in
[05-media-viewer-and-workspaces.md](05-media-viewer-and-workspaces.md) (§14.57–§14.70).

---

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

