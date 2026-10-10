# Architecture Document — Feature Decision Log: Media Viewer & Workspaces

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign**, aside from a handful of features below
that add their own small Domain/Application pieces (called out inline where relevant) — and is
recorded as a decision log per feature, not an implementation walkthrough; see git history and the
code's own comments (many cite these subsection numbers directly) for the detail.

Covers §14.57–§14.70. Preceded by
[01-workspace-navigation-and-search.md](01-workspace-navigation-and-search.md) (§14.1–§14.26),
[02-preview-decoration-and-status.md](02-preview-decoration-and-status.md) (§14.27–§14.33),
[03-monitoring-git-and-productivity.md](03-monitoring-git-and-productivity.md) (§14.34–§14.43), and
[04-search-persistence-and-file-tools.md](04-search-persistence-and-file-tools.md) (§14.44–§14.56).

---

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
