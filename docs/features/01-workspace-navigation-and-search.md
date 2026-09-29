# Architecture Document — Feature Decision Log: Workspace, Navigation & Search

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.1–§14.26. Continued in
[02-preview-decoration-and-status.md](02-preview-decoration-and-status.md) (§14.27–§14.33) and
[03-monitoring-git-and-productivity.md](03-monitoring-git-and-productivity.md) (§14.34–§14.40).

---

### 14.1 Scope and Product Decisions

Splits own tabs, not the reverse: a fixed `SplitLayout` (`Single`, `TwoVertical`, `TwoHorizontal`,
`FourGrid` — exactly these four, no arbitrary nesting) produces up to 4 fixed pane slots, each
independently owning its own tabs and navigation toolbar. Panes keep their tab state even when
hidden by a layout showing fewer panes. The tag panel and media preview are single shared instances
retargeted to whichever pane/tab has focus, mediated by `WorkspaceController`.

### 14.2 Key Architectural Decisions

* Domain/Application layers are unchanged — panes/tabs are pure UI-session state.
* `FileNavigationUseCase` stays stateless; one shared instance serves every pane/tab.
* Exactly one `MainWindow`; no pane/tab component ever opens a second top-level window.
* `WorkspaceController` owns exactly 4 `WorkspacePaneViewModel`s (flat, fixed, no composite tree);
  the active `SplitLayout` only controls which are visible.

### 14.3 New Components by Layer

`src/domain`/`src/application` — unchanged. Key new `adapters/viewmodels` types: `SplitLayout`,
`WorkspacePaneId` (plain enums), `WorkspaceLayoutTopology::visiblePanes(SplitLayout)` (pure,
unit-tested), `TabViewModel` (owns one `NavigationHistory` + `ViewMode` + `FileListModel` + current
path — a tab's full state; Command-pattern navigation history), `WorkspacePaneViewModel` (owns an
ordered `TabViewModel` list for one pane), `WorkspaceController` (the Mediator — owns the 4 panes,
current layout, and focused pane; the hook point for retargeting shared ViewModels). New
`src/ui/widgets` types: `WorkspaceLayoutWidget` (builds nested `QSplitter`s per layout, hiding
rather than destroying panes on a layout switch), `WorkspacePaneWidget` (one pane's toolbar +
tabbed `FileBrowserView`s).

### 14.4 Component Relationship

```
MainWindow
 └── WorkspaceController (owns SplitLayout + focused pane)
      ├── WorkspacePaneViewModel × 4 ── TabViewModel × N ── FileListModel
      ├── TagListViewModel (shared) ◄── retargeted to focused pane's active tab
      └── MediaPreviewViewModel (shared) ◄── retargeted likewise
                                                │
                                                ▼
                                  FileNavigationUseCase (shared, stateless)
                                                │
                                                ▼
                                  IFileSystemRepository (shared)
```

### 14.5 SOLID / Pattern Rationale

SRP separates a tab's state (`TabViewModel`), a pane's tab bookkeeping (`WorkspacePaneViewModel`),
and cross-pane mediation (`WorkspaceController`) into three types with three distinct reasons to
change. New layouts extend `SplitLayout`/`WorkspaceLayoutTopology` without touching the ViewModels
(OCP). Ports stay pane-unaware (ISP). Panes never reference each other directly, only emit signals
(DIP). Patterns: Factory Method (`CompositionRoot`), Mediator (`WorkspaceController`), Observer (Qt
signals/slots), Command (`NavigationHistory`).

### 14.6 Usage Rules

`FileNavigationUseCase` stays stateless; cross-pane coordination goes only through
`WorkspaceController`; ViewModels are created only via `CompositionRoot`'s factories (or
`WorkspacePaneViewModel::addTab()` for a session-time new tab) — UI code never constructs them
directly; `SplitLayout`/`WorkspacePaneId`/`WorkspaceLayoutTopology` stay plain C++, with the
`QSplitter` tree built from them separately; exactly one `MainWindow`; no `WorkspacePaneId`/
`SplitLayout` may appear in `src/domain`/`src/application`; no pane/tab component creates its own
thread; `NavigationHistory` does not itself re-validate paths or do I/O — callers do that.

### 14.7 Navigation History and Threading

History is per-tab (one `NavigationHistory` per `TabViewModel`, discarded when the tab closes; no
global/shared back-forward). Threading is unchanged from §5: concurrent tab navigations are
independent tasks on the same shared repository/pool, results marshaled back only to the
originating tab's model.

### 14.8 Deferred / Future Work

Live/continuous session autosave (save-on-close is implemented, §14.14); cross-pane drag-and-drop.
An empty pane revealed by a layout switch may auto-seed a tab at the focused pane's path, or start
with just a "+" button — either is acceptable.

### 14.9 Tag Panel and Selection Tracking

The shared `TagListViewModel` is owned by `WorkspaceController` and retargeted (`setActiveTab`) on
every focus/tab change, refreshing three sections: the focused tab's folder-plus-ancestor tags,
live tag search results, and the selected item's tags. Selection is per-tab state
(`TabViewModel::selectedEntries()`, a `std::vector<FileNode>` since §14.16), cleared on every
navigation. The tagging target resolves to: the single selected entry; the current folder itself
(via `IFileSystemRepository::stat`) if nothing is selected; or no target if more than one item is
selected (multi-item tagging isn't built). `SQLiteTagRepository` is the first concrete
`ITagRepository` implementation, introduced for this feature. Kept synchronous on the UI thread
(§5). Tag chips render via a `FlowLayout` (wrapping row layout) and `TagChipWidget`
(read-only/addable/removable/selectable pill styles).

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

### 14.14 Session Persistence (Window Geometry, Layout, Tabs, View & Sort)

The split-window session (layout, each pane's tabs — path/view mode/sort — focused pane, window
geometry) persists to a JSON file (`config.json`) rather than `QSettings`, so it can hold
structured per-pane/per-tab state.

* `WorkspaceConfig`/`TabConfig`/`PaneConfig` (`adapters/viewmodels/WorkspaceConfig.h`) are plain C++
  snapshots; `WorkspaceController::captureConfig()`/`restoreFromConfig()` convert to/from live
  pane/tab state.
* `AppConfig`/`AppConfigStore` (`adapters/config/`, new module) are the Qt/JSON persistence
  boundary — the one place this data is allowed to touch `QByteArray`/`QJsonDocument`. Window
  geometry round-trips through Qt's own `saveGeometry()`/`restoreGeometry()` rather than a
  hand-rolled struct. Enum values serialize as readable strings, not raw ints. `load()` is tolerant
  of a missing file or any malformed field, falling back to defaults field-by-field; `save()` writes
  to a temp file and renames over the target so a crash mid-write can't corrupt the previous file.
* **Save-on-close only**, from `MainWindow::closeEvent()` — no periodic/live autosave in this
  increment, including the one shared set of Details-view column widths (also captured only at
  close, from whichever tab/pane is currently focused).
* **Not built here:** live autosave; per-folder column widths (this app keeps one shared set
  app-wide); migrating the `QSettings`-backed shell-extensions toggle (§14.13) into `config.json`; a
  config schema-migration framework beyond a reserved `"version"` field; bookmarks/pinned folders;
  thumbnail cache size limit.

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

### 14.17 Tag Manager Dialog

A standalone modal (`TagManagerDialog`/`TagManagerViewModel`) for global tag CRUD — rename, delete
(cascading via the schema's `ON DELETE CASCADE`, §7), add without also tagging anything — distinct
from the tag panel (§14.9), which only attaches/detaches tags to a specific selection. Built
entirely on `TagManagementUseCase`'s existing `createTag`/`updateTag`/`deleteTag`/`searchTags`; no
Domain/Application/schema change. Reuses the §14.9 `FlowLayout`/`TagChipWidget` machinery. No
automated coverage beyond the already-tested use case (§11).

### 14.18 Filename Search (Per-Tab, Recursive)

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

### 14.23 Startup/Exit Logging and Fatal Exception Handling

A `Logging` module (`adapters/logging`) wraps spdlog behind `Logging::init/shutdown/log` (Debug/
Info/Warning/Error, timestamped, rotating file sink under `%LOCALAPPDATA%/Exp-LORer/logs/` plus a
console sink in debug builds) — the sole spdlog call site (§6). At this stage it is called only
from `main.cpp`, logging one line at startup (after `init()`) and one at clean exit (before
`shutdown()`); no call sites exist yet elsewhere in the app.

Any uncaught C++ exception is caught, logged at Error level, and shown to the user in a modal
dialog (title, exception message, a single "Exit" button) before the process terminates —
`ExceptionHandler::handleFatal` (`src/app`), invoked from two places: a top-level `try/catch`
around `main()`'s body (covers setup before the event loop starts, e.g. `SQLiteTagRepository`
failing to open the DB) and `ExpLorerApplication::notify()` (a thin `QApplication` subclass
overriding `notify()` to catch exceptions thrown during Qt event dispatch — input events, paint
events, queued slot invocations — while `app.exec()` is running).

**Scope:** main-thread exceptions only, consistent with all Port calls still being synchronous on
the UI thread in v1 (§5) — there is no second thread whose exceptions would need separate handling
yet. **Not built:** call-site logging beyond the two startup/exit lines above; an `ILogger` Port for
Domain/Application to log through (deferred until those layers actually need to log — `Logging` is
plain infrastructure, not wired through the composition root); native/SEH crash handling for
non-C++-exception crashes (e.g. access violations); per-thread exception handling for a future
background-dispatch thread (§5's deferred background-dispatch work would need its own try/catch at
each thread's entry point, since exceptions can't cross threads). `ExceptionHandler`/
`ExpLorerApplication` are UI-layer bootstrap code, out of CTest scope per §11 (same posture as the
other UI carve-outs) — smoke-tested manually; `Logging` itself gets an adapter test (`:memory:`-style
real temp directory, per §11) verifying `init()`/`log()`/`shutdown()` produce a log file with the
expected content.

### 14.24 Bottom Panel and Integrated Terminal

A VS Code-style collapsible panel at the bottom of every tab's own page (this app's tab, i.e. one
page of a pane's `QTabWidget` — not a top-level window), showing a strip of "panel tabs"; only
"Terminal" exists in v1, built so more (e.g. Problems/Output) can be added later without new
plumbing. Collapsed by default; the first time a tab's Terminal panel tab is clicked, a `cmd.exe`
session is lazily spawned in that tab's current folder at that moment — it does not follow later
navigation in the same tab, and is not restarted by later clicks (collapsing/re-expanding just
hides/shows the still-running session). Closing the tab kills the process.

* **No Application-layer Port.** Like `Logging` (§14.23), this has no Domain/Application business
  logic to abstract behind an interface, so it's plain UI-adjacent infrastructure with no
  `CompositionRoot`/use-case involvement.
* **`src/adapters/terminal`** (new module, `explorer_adapters_terminal`): `AnsiTerminalBuffer` — pure
  logic (no Qt Widgets, no OS calls), a resizable cell grid + cursor + capped scrollback, fed raw
  bytes via `feed()` and interpreting a practical VT/ANSI subset (cursor motion, erase-in-line/
  display, insert/delete/erase-character (ICH/DCH/ECH — needed for e.g. `cmd.exe`'s in-place line
  redraw during history recall, which shifts/erases characters rather than always erase-and-
  rewriting the whole line), 16-color SGR) — no 256-color/truecolor. The one piece of this feature
  that's pure and GTest-covered (`tests/adapters/AnsiTerminalBufferTest.cpp`), same posture as
  `DriveLabel`/
  `WorkspaceLayoutTopology`. `WindowsConPtyProcess` (Windows-only, same "Windows first" posture as
  `ShellContextMenuProvider`) — a `QObject` wrapping ConPTY (`CreatePseudoConsole` + `CreateProcessW`
  of `%ComSpec%`); runs one background `std::thread` blocking on `ReadFile` of the output pipe,
  posting each chunk back to the UI thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
  as `dataReceived(QByteArray)` — the only cross-thread hop, keeping ConPTY's blocking I/O off the UI
  thread (§5); emits `processExited(int)` when the pipe closes. `writeInput`/`resize` are synchronous
  UI-thread calls (`WriteFile`/`ResizePseudoConsole`), the same accepted posture as `ShellExecuteW`
  (§14.11). No `IPtyProcess` interface: Qt doesn't support pure-virtual signals, and nothing outside
  the UI layer needs to mock this (§11 already excludes UI from CTest) — a future Linux
  `PosixPtyProcess` is deferred until Linux support is actually built, at which point an interface
  can be introduced alongside it.
* **`src/ui/widgets`**: `TerminalWidget` — owns one `AnsiTerminalBuffer` + one `WindowsConPtyProcess`,
  paints the grid with a monospace font, translates key events to VT byte sequences, and resizes the
  pty on `resizeEvent`. On process exit, shows an inline restart prompt rather than auto-restarting.
  It also owns keyboard-only text selection and clipboard state — an anchor/active pair of
  `{logicalRow, column}` (logical row spanning scrollback + live grid, the same indexing
  `paintEvent` already uses to address either) tracked entirely in `TerminalWidget`, not
  `AnsiTerminalBuffer`, since it's interaction/rendering state rather than terminal emulation state:
  Shift+Left/Right extends a character selection, Ctrl+Shift+Left/Right a word selection (word
  boundaries computed within one row only — no cross-line word semantics) while *also* forwarding
  the word-jump VT sequence (`CSI 1;5D`/`CSI 1;5C`) so the shell's line-edit cursor moves in step
  with the extending selection. The local word-jump mirrors the console's own two-phase word-left/
  word-right (skip the adjacent whitespace run, then skip the word run beyond it, both within one
  keypress) rather than stopping at the first run boundary — a single-phase skip lands half a word
  short of where the shell's own cursor ends up, and the two silently drift apart over repeated
  presses. On the live input row, backward selection (both Shift+Left and
  Ctrl+Shift+Left) additionally stops at the shell prompt instead of walking into the
  current-directory text before it — the first `>` on that row (an illegal character in a Windows
  path, so it can only be the default `$P$G` prompt's terminator) is treated as a hard left
  boundary. Symmetrically, forward selection (Shift+Right and Ctrl+Shift+Right) stops at the end of
  the typed command instead of walking into the untyped blank cells that fill out the rest of the
  row — the boundary is the last non-space column on that row, widened to the selection anchor's own
  column when the anchor sits on that row (the anchor is planted at the cursor cell, one column past
  the last typed character, so without the widening a backward selection could never shrink all the
  way back to its own starting point via Shift+Right). Ctrl+A selects the whole retained buffer, Ctrl+Shift+C/V copy/paste
  via `QGuiApplication::clipboard()`, and Ctrl+Left/Right (no Shift) forwards the same word-jump
  sequence without touching any local selection (clearing one if active). Deliberately
  keyboard-only — no mouse drag-to-select.
  `BottomPanelWidget` — the generic collapsible panel shell: a header strip of checkable panel-tab
  buttons over a `QStackedWidget` of panel-tab content, starting collapsed; emits `expansionChanged`
  (for the owning splitter to resize) and `panelTabFirstActivated` (for lazy terminal start), staying
  itself splitter-agnostic. `WorkspacePaneWidget::addPageForTab` wraps its existing per-tab
  browse/search/advanced-search `QStackedWidget` and a `BottomPanelWidget` in a vertical `QSplitter`
  as the actual tab page; `onViewModeChanged`/`currentColumnWidths` unwrap `splitter->widget(0)`
  instead of assuming the tab page is the `QStackedWidget` directly.
* **Not built:** a Linux backend; mouse drag-to-select (selection is keyboard-only, see above);
  multiple terminal instances per tab; 256-color/truecolor; persisting panel open/closed state or
  size across restart (no `WorkspaceConfig`/§14.14 involvement — resets every launch); the
  terminal's directory following subsequent navigation in its tab (only the directory at
  first-expand time). No automated coverage beyond `AnsiTerminalBuffer` — ConPTY spawning and the
  painting/input/selection/clipboard widget code fall under the existing §11 UI/OS-shell carve-out,
  smoke-tested manually.

### 14.25 Right Panel (Tabbed, Collapsible)

The window's right-hand side panel uses the same VS Code-style collapsible-panel shell as the
bottom panel (§14.24), on the horizontal axis instead of the vertical one. `RightPanelWidget`
(`src/ui/widgets`) mirrors `BottomPanelWidget`'s mechanics — checkable panel-tab buttons over a
`QStackedWidget`, starting collapsed, `expansionChanged`/`panelTabFirstActivated` signals,
splitter-drag detection in `resizeEvent` — but its header strip is a vertical column of buttons on
the widget's left edge (next to the workspace) rather than a horizontal row on top, since the
collapse axis is width (`collapsedWidth()`/`preferredExpandedWidth()`) rather than height. It is a
sibling class, not a shared abstraction with `BottomPanelWidget`: the two header strips lay out
buttons in opposite directions and the two widgets sit in splitters of opposite orientation, so
forcing one orientation-parameterized base class for two call sites wasn't worth the indirection.

`MainWindow::createWorkspace` wraps the existing `TagPanelWidget` (§14.9/Specification.md "Tag
Panel") as the content of a single "Tags" panel tab, added to a `RightPanelWidget` placed as the
second child of the central horizontal `QSplitter` (in place of `TagPanelWidget` directly). The
splitter uses explicit `setSizes`/`setCollapsible(0, false)` plus a `RightPanelWidget::
expansionChanged` handler that resizes the splitter, the same posture as the bottom panel's
splitter wiring in `WorkspacePaneWidget::addPageForTab` — no stretch factors. `TagPanelWidget`
itself is unmodified; only its container changed. Only "Tags" exists in v1, but more tabs can be
added later without further plumbing, same intent as the bottom panel's Terminal-only v1 scope.
Like the bottom panel, expanded/collapsed state is not persisted across restarts.

### 14.26 Right-Panel File Preview (Preview Tab)

A second `RightPanelWidget` tab, "Preview" (§14.25's panel already supports more than one tab with
no further plumbing), showing content for whatever the focused pane/tab currently has selected —
or its browsed folder when nothing is selected, same resolution rule `TagListViewModel::
resolveTarget()` already uses for the tag panel (§14.9). This is also the first concrete
implementation of `adapters/media` (previously `.gitkeep` only) and the first feature to actually
do background-thread Port dispatch (§5).

* **Scope by file type:** a directory shows its immediate children only (directories first, then
  files, both alphabetical — not a recursive tree); a recognized text/source extension
  (txt/html/xml/json/md/ini/log/yaml/csv, common source extensions cs/cpp/h/hpp/c/py/js/ts/css/…)
  shows a capped byte-prefix of its content, falling back to "unsupported" if a `\0` appears in
  that prefix (binary guard); an image extension (jpg/png/bmp/gif/webp) shows a scaled preview
  decoded via **libvips** (new vcpkg dependency), per the product requirement to use that library
  specifically rather than Qt's own image codecs; a video extension (mp4/mkv/avi/mov/…) shows a
  scaled **static poster frame** decoded via FFmpeg (already a dependency) — no inline playback,
  which belongs to the still-unbuilt full-window QML viewer (Specification §2.3/§2.4) instead.
  Anything else shows "no preview available."
* **`FilePreviewUseCase`** (new, `src/application`) — stateless, depends on
  `IFileSystemRepository` (folder listing via the existing `listDirectory`; a new
  `readFilePrefix(path, maxBytes)` method for text) and `IMediaDecoder` (image/video thumbnail
  bytes, reusing the existing `generateThumbnail(path, maxWidth, maxHeight)` contract) — never on
  `MediaProcessingUseCase` or any other use case, preserving this layer's "depend on Ports only"
  testability posture (§11). Returns a new Domain entity, `FilePreview` (kind + folder
  entries/text/image-or-poster bytes).
* **`adapters/media`** (new target `explorer_adapters_media`): `MediaDecoder` — the concrete
  `IMediaDecoder` dispatching by extension to `VipsImageDecoder` (images, libvips end to end:
  decode, resize, JPEG-encode) or `FFmpegMediaDecoder` (video, one representative frame via
  libavformat/libavcodec, scaled via libswscale, JPEG-encoded via libavcodec's MJPEG encoder) —
  each self-contained, not cross-calling the other, keeping the two backends independently
  swappable. `ThumbnailCache` + `CachingMediaDecoder` (§8) sit in front as a transparent decorator,
  so `FilePreviewUseCase` never sees caching logic.
* **`FilePreviewViewModel`** (new, `adapters/viewmodels`) — the shared ViewModel, owned by
  `WorkspaceController` and retargeted via `setActiveTab()` exactly like `TagListViewModel` (§14.9)
  — a **distinct component from the still-unbuilt `MediaPreviewViewModel`** named in §2.3, which is
  QML-facing state for the future full-window viewer, not this panel; the two must not be
  conflated. Owns one dedicated background `QThread` + `FilePreviewWorker` (one always-alive
  worker, not a pool — only one active preview needed at a time), the same "one background thread,
  results marshaled back via a queued signal" shape `WindowsConPtyProcess` already established
  (§14.24). A `setPanelActive(bool)` slot, driven by the widget's `showEvent`/`hideEvent`, stops
  background work while the Preview tab isn't the visible one and catches up immediately on
  reactivation. Cancellation is a generation-id fence (§5) — switching the selection bumps a
  counter, and a result that arrives for a stale generation is dropped, never shown; the
  in-flight libvips/FFmpeg call itself still runs to completion, since neither library exposes a
  mid-call abort. A small in-memory LRU (~20 entries) caches `Text`/`Folder` results only —
  image/video results are already cached on disk by `CachingMediaDecoder`, so caching them a
  second time here would be redundant.
* **`PreviewPanelWidget`** (new, `src/ui/widgets`) — a `QStackedWidget` switching between an image/
  poster `QLabel`, a read-only monospace `QPlainTextEdit` for text, a flat `QListWidget` (existing
  `QFileIconProvider` icons) for the one-level folder listing, and loading/unsupported/error
  states, driven by `FilePreviewViewModel`'s signals. `MainWindow::createWorkspace` adds it as a
  second `RightPanelWidget` tab next to "Tags" — no change needed to `RightPanelWidget` itself.
* **Not built:** video playback; a recursive/multi-level folder tree; animated-GIF playback beyond
  a static first frame (goes through the image path); full-content display beyond the byte cap
  (syntax highlighting of the shown prefix is covered by §14.27); persisting the Preview tab's
  expanded/collapsed state (matches §14.24/§14.25's existing unpersisted posture); a Linux
  media-decoding backend.
