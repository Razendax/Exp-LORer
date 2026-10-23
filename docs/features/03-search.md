# Architecture Document — Feature Decision Log: Search

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign**, aside from a couple of features below
— and is recorded as a decision log per feature, not an implementation walkthrough; see git
history and the code's own comments (many cite these subsection numbers directly) for the detail.

Covers §14.18–14.20, §14.38, §14.42–14.46, §14.56, §14.87, §14.104 — per-tab filename/advanced/
tag-integrated search, the unified left-panel Search tab, async advanced search, quick-search
match-mode/skip-git-ignored toggles, debounced quick search, advanced-search match-mode toggles,
hierarchical-tag search matching, and content ("Find in Files") search. Section numbers are **not**
contiguous with this file's neighbors — they were assigned chronologically as each feature was
built and are kept stable since code comments cite them directly. See
[Architecture.md](../Architecture.md) §14 for the full topic index across all nine files.

---
### 14.18 Filename Search (Per-Tab, Recursive)

> Superseded in UI placement by §14.42 (below):
> the toolbar search box described below was removed and this search entry point was consolidated
> into the left panel's "Search" tab, now always async per §14.38's machinery rather than
> synchronous. The recursive-scan/caching mechanics below are otherwise unchanged and still
> accurate for how a search is actually executed.

Recursive filename/extension search scoped to a tab's current folder, triggered from a toolbar
search box and rendered inside the tab it ran against (search state lives on `TabViewModel`, not a
separate panel). `IFileSystemRepository::listDirectoryRecursive` (new, symmetric to
`listDirectory`) walks via `fs::recursive_directory_iterator`, skipping unreadable subtrees rather
than failing the whole call; it rejects the "This PC" sentinel outright. A tab owns a second
`FileListModel` for results plus a cached recursive snapshot: the initial Enter triggers one disk
scan, and further typing re-filters the cached snapshot only — no further I/O. Reuses
`FileBrowserView`/`FileListModel` as-is (icons, sorting, multi-selection, hotkeys, context menu all
come for free). Synchronous on the UI thread (§5) — a very large folder briefly freezes the UI on
the initial scan, accepted. Pure filtering logic and the repository method are GTest-covered; the
ViewModel/UI state is not (§11).

### 14.19 Advanced (Criteria) Search Pane

> §14.42 relocates only this pane's *entry-point button* (the former toolbar "Advanced Search..."
> action) into the left panel's new "Search" tab, below its search bar — everything described
> below (the pane itself, its fields, its per-tab in-place results view, and §14.20's tag
> integration) is otherwise unchanged.

A second, richer search entry point alongside §14.18 (kept unmodified) — Name / Min size / Max size
criteria, results always rendered in a taller, match-highlighting Details-like view
regardless of the tab's own `ViewMode`. Opening the pane shows a plain non-recursive listing of the
current folder (not a search — no snapshot cached, no criteria applied) so it doesn't open empty;
the first actual search (Search press, Enter, or a tag-search click) is what triggers the real
recursive scan. Mutually exclusive with quick search on the same tab; each
mode's state persists independently while hidden. `SearchCriteria` is a plain Application-layer
struct; `FileNavigationUseCase::filterByCriteria` composes two pure filters (`filterBySizeRange`,
plus the existing `filterByName`), each a no-op when its part of the criteria is unset. The Name
field accepts `*`/`?` wildcards (`*` = any run of characters, `?` = exactly one character) — a
query containing neither is implicitly wrapped as `*query*` so the historical "contains" behavior
is unchanged for plain text; a query containing either is matched as a full anchored glob against
the whole name via **`src/domain/GlobMatch.h`/`.cpp`** (new, pure, no framework deps — same posture
as `PathUtf8`/`PathContainment`), which extracts the codepoint-aware glob matcher §14.30's
`FileNamePattern` already implemented, so the two features share one matching implementation
instead of duplicating it. There is deliberately no separate Extension field — `*.ext`-style Name
queries cover that case. `TabViewModel` gets a parallel search-state block (its own
`FileListModel`, cached snapshot, criteria) alongside §14.18's, with each mode's entry point exiting
the other. `SearchResultDelegate` bolds/highlights the matched substring in the Name column, the
same manual multi-`drawText` technique `FileTileDelegate` uses. `FileBrowserView` gains an opt-in
`AdvancedSearchResults` display mode rather than a new widget class, so existing
selection/hotkey/context-menu wiring is reused as-is. The recursive scan itself dispatches to a
background worker thread (§5, §14.38) rather than running on the UI thread; re-filtering an
already-cached scan (e.g. editing the Name field without pressing Search again) stays synchronous,
since that step is pure in-memory filtering with no I/O. The pure filters are GTest-covered; the
ViewModel/UI state is not (§11). Not built: a "Location"
disambiguation column; cancellation of the recursive scan; persisting criteria across restarts
(resets like §14.18's quick search). Tag criteria (a fifth, ANDed criterion) are covered
separately by §14.20, layered on top of this section rather than modifying it.

### 14.20 Tag-Integrated Advanced Search

Extends §14.9 (tag panel) and §14.19 (advanced search) so the two features interoperate: every
tag chip anywhere in `TagPanelWidget` is clickable, and clicking one opens Advanced Search on the
*focused* tab with that tag applied as a criterion; Advanced Search itself gains its own tag
search box so criteria can be built without visiting the tag panel at all. Search results must
then satisfy file-property criteria (name/size, §14.19) **and** tag criteria together.

* **`SearchCriteria` gains `std::vector<Tag::Id> tagIds`** (AND semantics, mirroring
  `ITagRepository::findFilesWithAllTags`; empty = no tag filter).
* **Tag-first result building.** Whenever `tagIds` is non-empty, the tag DB query is the *source*
  of the result set, not a post-filter over a recursive scan — a bare tag click (no Search-button
  press, no other criteria) must answer purely from the database, with zero disk tree-walking.
  `TabViewModel::runAdvancedSearch` composes this as:
  1. `TagManagementUseCase::findFilesWithAllTags(tagIds)`.
  2. Keep only associations whose `filePath()` satisfies
     `isPathWithinOrEqual(path, m_advancedSearchRoot)` (`src/domain/PathContainment.h`) — this is
     what confines tag results to the current tab's folder subtree, replacing the old scan-based
     scoping now that there's no scan to intersect against.
  3. `FileNavigationUseCase::stat(path)` per surviving path to obtain a `FileNode` (name/size/mtime
     for display and for stage 4 below); a `stat` failure (file moved/deleted/renamed since
     tagging — no hash-based relocation attempted) drops that entry silently. This is a bounded
     number of single-file `stat` calls, not a tree walk, so it stays cheap even for large tagged
     sets (Specification.md:303's "~100,000 tagged files: ~500ms" target is about this DB-driven
     path, not a recursive scan).
  4. `FileNavigationUseCase::filterByCriteria` (name/size) runs on this candidate list as a
     **secondary** filter — the two stages still compose as an AND, same as before, just with the
     tag stage now first and cheaper.
  When `tagIds` is empty, the pre-existing path is unchanged: recursive scan (cached in
  `m_advancedSearchSnapshot`) → `filterByCriteria`. `FileNavigationUseCase::filterByPaths` (the
  older scan-then-intersect filter) has been removed as dead code by this change.
* **`TabViewModel` gains a `TagManagementUseCase&` dependency** (threaded through
  `WorkspacePaneViewModel`'s and `WorkspaceController`'s constructors, which already receives one
  for the shared `TagListViewModel`), plus:
  * `addTagSearchCriterion(Tag::Id)` / `removeTagSearchCriterion(Tag::Id)` — mutate
    `SearchCriteria.tagIds` and (re)run the search; opens the panel first if it wasn't already
    active (same single-active-mode invariant as `showAdvancedSearchPanel`).
  * `searchTagsForCriteria(query) const` / `resolveTagCriteria() const` — pure queries (no state
    mutation), the same posture as §14.21's `suggestFolders`: the widget layer calls them
    on-demand and pushes the result back into itself, rather than a ViewModel class mediating.
  * A shared private `runAdvancedSearch(forceRescan)` now backs `startAdvancedSearch`/
    `updateAdvancedSearchCriteria` *and* the two tag-criterion methods above. A new
    `m_advancedSearchFullScanDone` flag (separate from `m_advancedSearchRoot.empty()`) tracks
    whether the *current* root actually has a cached recursive-scan snapshot, as opposed to having
    only been set by a tag-first run (which sets the root for scoping purposes but never scans) —
    the name/size-only branch rescans when `forceRescan || m_advancedSearchRoot.empty() ||
    !m_advancedSearchFullScanDone`, so removing the last tag criterion correctly forces the
    recursive scan it's now relying on for the first time, rather than reusing stale/absent state.
    This preserves §14.19's "editing before the first Search does not scan" contract while letting
    the tag-click entry points (which have no Search-button press to hang a scan off of) trigger
    their own DB-driven result immediately. `showAdvancedSearchPanel`'s own non-recursive
    current-folder listing (§14.19) is unaffected — it still leaves `m_advancedSearchRoot` unset.
  * A new `advancedSearchCriteriaChanged(const SearchCriteria&)` signal, since criteria can now
    change from three independent places (the existing Name/size/extension fields, Advanced
    Search's own new tag search box, and a `TagPanelWidget` click on a different widget entirely)
    that all need the tag-criteria chip row to stay in sync.
* **`TagChipWidget` becomes clickable for every `Kind`**, not just `Selectable` — `ReadOnly`/
  `Addable`/`Removable` now also emit `clicked(Tag::Id)` on a body click (unaffected: clicks on
  the separate `+`/`x` child `QToolButton`s). This one change powers both the right panel
  (requirement) and Advanced Search's new "matching tags" row, which reuses `Kind::ReadOnly`
  verbatim ("same as the right panel but without a + button").
* **`TagListViewModel::requestTagSearch(Tag::Id)`** — new slot, forwards to
  `m_activeTab->addTagSearchCriterion(tagId)`. Since `WorkspaceController::retargetFocusedTab()`
  already keeps `TagListViewModel::m_activeTab` pointed at the focused pane's active tab (§14.9),
  this automatically targets "the currently active tab" with no new retargeting logic.
* **`SearchCriteriaPanel`/`SearchResultsPane`** gain a tag search box (results shown as
  `Kind::ReadOnly` chips, click-to-add) and a tag-criteria chip row (`Kind::Removable`,
  click-x-to-remove), wired in `WorkspacePaneWidget::addPageForTab` the same way the existing
  Name/size/extension fields are.
* **Not built:** persisting tag criteria across restarts (matches §14.19's existing exclusion for
  its other fields); surfacing which specific tag caused a given result to match; batching/
  parallelizing the per-path `stat()` calls in the tag-first path (accepted as a bounded cost — since
  §14.38 it runs on `AdvancedSearchWorker`'s background thread rather than the UI thread, same
  dispatch as §14.19's own recursive scan, but the per-path `stat()` loop itself is still sequential
  within that one worker call). `TabViewModel`/widget changes stay under §11/§14.19's existing
  "ViewModel/UI is not CTest-covered" posture.

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

### 14.87 Advanced Search: Hierarchical ("/"-Nested) Tags, Exact/Include-Children Toggle

Tags gain a parent/child naming convention — `"game/action"`'s parent is `"game"` — and §14.20's
tag criteria gain a toggle choosing whether a selected tag matches only itself or itself plus every
nested child. Unlike this file's other entries, this one **does** touch Domain and Application
(the hierarchy is a name convention, so something has to understand `/` as a separator), not just
UI/Adapters.

* **No schema change.** `Tags.name` already stores an arbitrary string; `"game"` need not exist as
  its own row for `"game/action"` to be treated as its child — the relationship is derived from
  the name at query time, never persisted separately.
* **`Tag::create` (`src/domain/Tag.cpp`)** rejects a name with a leading `/`, trailing `/`, or `//`
  (an empty segment) alongside its existing empty-name check, since such a name could never be a
  meaningful hierarchy level. Covers both create and rename, since rename always goes through
  `Tag::create` first (e.g. `TagManagerViewModel::renameTag`).
* **`TagHierarchy::isSelfOrDescendant(name, ancestorName)`** (new `src/domain/TagHierarchy.h`/
  `.cpp`, pure, no Qt/SQLite) — true when `name == ancestorName` or `name` starts with
  `ancestorName + "/"`. Case-insensitive via the existing `UnicodeCaseFold::foldUtf8`, the same
  Unicode-safe fold `GlobMatch`/`PathContainment` already use, rather than a new ASCII-only one.
* **`ITagRepository::findFilesWithAnyOfTagGroups(tagIdGroups)`** (new port method) — AND-of-OR:
  a file matches when it has at least one id from every group in `tagIdGroups`. `SQLiteTagRepository`
  implements it as one `EXISTS (SELECT 1 FROM FileTags ft WHERE ft.file_id = f.id AND ft.tag_id IN
  (...))` per group, ANDed in the `WHERE` clause — the AND-of-OR generalization of §7's plain
  `IN (...) / HAVING COUNT(DISTINCT tag_id) = N` AND-of-exact filtering. Empty `tagIdGroups`
  short-circuits to an empty success, same as `findFilesWithAllTags`'s empty-`tagIds` guard. Each
  matched row yields one `FileTagAssociation` per group, tagged with that group's first
  (originally-selected) id — callers only ever read `association.filePath()`, matching
  `findFilesWithAllTags`'s existing contract.
* **`TagManagementUseCase::findFilesWithTagsIncludingChildren(tagIds)`** (new) builds each
  requested id's group — itself plus every tag `TagHierarchy::isSelfOrDescendant` of it, found via
  a single `allTags()` call — then delegates to `findFilesWithAnyOfTagGroups`. A stale id (deleted
  since selected) becomes an **empty** group rather than being dropped, so that criterion is simply
  unsatisfiable — the same "zero rows" fate a stale id already gets today in `findFilesWithAllTags`.
  `findFilesWithAllTags` itself is untouched and still backs both Advanced Search's exact mode and
  the Media Viewer's tag filter (§14.72/14.73) unchanged.
* **`SearchCriteria.tagIncludeChildTags`** (new `bool`, default `false` — preserves today's
  exact-match behavior). `SearchCriteriaPanel` gets a new checkable `QToolButton`
  (`advancedSearchTagIncludeChildrenButton`) next to `m_tagSearchEdit` in the tag-search row (not
  the Name-matching `matchOptionsRow`, since this only affects tag matching), wired to
  `onAnyFieldEdited` exactly like the other toggle buttons; `currentCriteria()`/`setCriteria()`
  read/write it the same way they already handle `nameOptions.skipGitIgnored`.
* **`AdvancedSearchWorker`/`AdvancedSearchService`** gain
  `fetchTagFirstCandidatesIncludingChildren`/`requestTagFirstCandidatesIncludingChildren`, copies of
  the existing `fetchTagFirstCandidates`/`requestTagFirstCandidates` that call
  `findFilesWithTagsIncludingChildren` instead of `findFilesWithAllTags`.
* **`TabViewModel::runAdvancedSearch`'s tag-first branch** picks between the two
  request methods based on `m_advancedSearchCriteria.tagIncludeChildTags`; its cache-reuse check
  gains a parallel `m_advancedSearchCachedTagIncludeChildren` comparison alongside
  `m_advancedSearchCachedTagIds`, so switching the toggle never silently reuses the other mode's
  cached candidates. `updateAdvancedSearchCriteria` forces a rescan when the toggle changes, the
  same way it already does for `nameOptions.skipGitIgnored` (§14.56) — toggling changes which DB
  query answers the same tag-id set, so a cached result can never serve the other mode.
* **Not built:** the toggle does not reach the Media Viewer's own tag filter (§14.72/14.73), the
  Tag Panel, or the Tag Manager dialog — all three keep exact-name matching only, unchanged. No
  auto-creation of a parent tag when a nested one is created, and no tree/indentation UI anywhere
  tags are rendered — `TagChipWidget` still renders the full literal name.
* **Testing**: `tests/domain/TagTest.cpp` (segment validation) and new
  `tests/domain/TagHierarchyTest.cpp`; `tests/application/TagManagementUseCaseTest.cpp` (new
  `findFilesWithTagsIncludingChildren` cases, `MockTagRepository` gains the new port method);
  `tests/adapters/SQLiteTagRepositoryTest.cpp` (new `findFilesWithAnyOfTagGroups` cases). The
  `TabViewModel`/`SearchCriteriaPanel` toggle wiring stays under the existing §11 UI/ViewModel-glue
  carve-out — manual verification only.

### 14.104 Content Search ("Find in Files")

A new sibling to the existing filename Search tab, following the exact same layering shape end to
end — stateless Application use case → dedicated-thread Worker/Service pair → per-tab ViewModel
with generation-fenced async dispatch → a new left-panel tab widget → `MainWindow` focus-retargeting
glue — but as an independent, parallel pipeline: nothing about `TabQuickSearchViewModel`/
`AdvancedSearchWorker`/`SearchPanelWidget` changed.

* **Domain/Application** (`src/domain/SubstringMatch.h`/`.cpp`, `src/domain/BinaryContentDetector.h`/
  `.cpp`, `src/domain/ContentSearchMatch.h`, `src/application/search/ContentSearchOptions.h`,
  `src/application/search/ContentSearchUseCase.h`/`.cpp`): `SubstringMatch` is the one matcher mode
  that had no named symbol yet (`GlobMatch`/`WholeWordMatch`/`RegexMatch` each already had one) —
  `UnicodeCaseFold`-folded substring search, backing both the main query's plain "Contains" mode and
  the Include/Exclude fields' plain mode (`*`/`?` are literal here, no glob expansion).
  `BinaryContentDetector::containsNulByte` extracts the heuristic `FilePreviewUseCase::generatePreview`
  used to inline, now shared by both call sites. `ContentSearchUseCase` is stateless (one shared
  instance, constructed with `FileNavigationUseCase&` + `IFileSystemRepository&`):
  `collectSearchableContent` is the I/O-bound half (recursive listing, per-entry hidden/archive/
  Include/Exclude filtering, a capped `kMaxContentSearchBytes` (1 MiB) prefix read per file, binary
  skip, line splitting); `filterLines` is the pure, cheap, no-I/O half that applies the main query to
  already-collected lines; both share the private `textMatches` mode-dispatch helper (Contains/
  WholeWord/Regex, an invalid Regex pattern treated as "no match").
* **Adapters** (`src/adapters/git/GitIgnorePrunePredicateBuilder.h`/`.cpp`,
  `src/adapters/viewmodels/search/ContentSearchWorker.h`/`.cpp`,
  `ContentSearchService.h`/`.cpp`, `ContentSearchResultsModel.h`/`.cpp`,
  `TabContentSearchViewModel.h`/`.cpp`): `AdvancedSearchWorker::buildGitIgnorePrunePredicate`'s body
  was extracted into a free function so `ContentSearchWorker` reuses it instead of a second
  hand-copy. `ContentSearchWorker`/`ContentSearchService` mirror `AdvancedSearchWorker`/
  `AdvancedSearchService`'s shape (dedicated `QThread`, lazy worker-thread-owned
  `LibGit2StatusProvider`/`GitStatusUseCase` pair for the prune predicate) minus the tag-repository
  dependency, since content search has no tag integration. `ContentSearchResultsModel` is a 2-level
  `QAbstractItemModel` (file-header rows, each with child line rows) — deliberately simpler than
  `SearchResultsTreeModel`, since content search's spec calls for a flat "grouped by file" list, not
  a folder-hierarchy tree. **The one deliberate behavioral difference from filename search**, per
  spec: `TabContentSearchViewModel::updateQuery` always dispatches a fresh disk+content scan on every
  settled query-text edit — never a cached re-filter — because content search is "heavier." The only
  in-memory-only re-filter path is toggling Match Case/Whole Word/Regex on the main query with the
  query text unchanged (`setOptions` re-runs `ContentSearchUseCase::filterLines` over the last scan's
  cached `m_lastScanFiles` with no dispatch); changing `skipGitIgnored`/Include/Exclude always forces
  a fresh scan instead, since those change what gets *read*.
* **UI** (`src/ui/widgets/search/ContentSearchResultDelegate.h`/`.cpp`,
  `ContentSearchPanelWidget.h`/`.cpp`, `MainWindow::createContentSearchPanel`): a new "Find in Files"
  left-panel tab alongside Tree/Search. The delegate re-derives highlight spans at paint time
  (`QRegularExpression::globalMatch` for Regex mode) rather than consuming spans from a matcher —
  the same precedent `SearchResultDelegate` already set — and, unlike filename search's accepted
  "regex highlighting is a literal-substring approximation" gap (§14.44), does the
  `QRegularExpression` scan properly since this is new code with no legacy behavior to preserve.
* **Milestone 4 — preview-panel line jump**: double-clicking a line row
  (`ContentSearchPanelWidget::lineActivated` → `MainWindow::onContentSearchLineActivated`) navigates/
  selects the file (reusing `onContentSearchResultActivated`'s body) and records
  `{path, oneBasedLine}` in `MainWindow::m_pendingContentSearchLineJump`. `TextEditorWidget::goToLine`
  already existed (clamps/centers/moves the cursor); `TextEditPreviewWidget::goToLine` forwards to it;
  `PreviewPanelWidget::goToLine(path, oneBasedLine)` forwards further, but only when `path` matches
  `m_currentTextPath` (a new member set in `onPreviewReady` whenever the preview kind is Text, cleared
  for every other kind/state) — a stale request for a target the panel has since navigated away from
  is a silent no-op. `MainWindow` connects to `FilePreviewViewModel::previewReady` (added in
  `createWorkspace()` right after `PreviewPanelWidget`'s own constructor already connected its
  handler to the same signal — Qt's direct-connection ordering guarantees that handler runs first, so
  the editor is already populated by the time this one fires): if the pending jump's path/kind match
  the delivered preview, it calls `goToLine` and clears the pending jump. A second connection to
  `previewLoading()` (which only fires when the resolved target actually *changes*, never for a
  same-target directory-watch refresh) unconditionally clears any still-pending jump, since
  `onContentSearchLineActivated` always sets the pending jump *after* the navigation that triggers its
  own `previewLoading()` — so any further tick seen while a jump is still pending is necessarily a
  newer, different navigation superseding it.
* **Non-Goals carried from spec/plan**: no in-place replace, no archive search, no multi-pattern
  Include/Exclude, no persistence of query/options across restarts, no true scan cancellation (only
  generation-fenced discard of a stale reply), no pagination beyond the per-file
  `kMaxContentSearchBytes` prefix cap. Include/Exclude filtering happens on complete file candidates
  only, not as a directory-walk prune. Hidden-file exclusion is per-entry (`FileNode::isHidden()`),
  not a recursive ancestor check.
* **Testing**: `tests/domain/SubstringMatchTest.cpp`, `tests/domain/BinaryContentDetectorTest.cpp`,
  `tests/application/ContentSearchUseCaseTest.cpp` cover the pure pieces (`filterLines`'s three
  modes, `textMatches` against lines and relative paths, binary-skip, hidden-file skip, Include-AND-
  Exclude composition). `collectSearchableContent`/`ContentSearchWorker::scan` and all UI/ViewModel
  glue stay under the existing §11 carve-out — manual verification only.

