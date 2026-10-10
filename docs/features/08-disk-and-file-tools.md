# Architecture Document — Feature Decision Log: Disk & File Tools

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign**, aside from a couple of features below
that add their own small Domain/Application pieces (called out inline where relevant) — and is
recorded as a decision log per feature, not an implementation walkthrough; see git history and
the code's own comments (many cite these subsection numbers directly) for the detail.

Covers §14.39–14.40, §14.49, §14.51–14.53, §14.55 — customizable Details-view columns (and their
per-tab persistence), disk usage/folder size, the duplicate file finder, the disk fragmentation
viewer (and its Details-view fragments column), and bulk/batch rename. Section numbers are **not**
contiguous with this file's neighbors — they were assigned chronologically as each feature was
built and are kept stable since code comments cite them directly. See
[Architecture.md](../Architecture.md) §14 for the full topic index across all nine files.

---
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

