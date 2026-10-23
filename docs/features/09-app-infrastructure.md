# Architecture Document — Feature Decision Log: App Infrastructure

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.23, §14.36–14.37, §14.50, §14.57, §14.103 — startup/exit logging and fatal exception
handling, favorites, the notification system, Light/Dark theme, per-process environment variables,
and the Windows installer.
Section numbers are **not** contiguous with this file's neighbors — they were assigned
chronologically as each feature was built and are kept stable since code comments cite them
directly. See [Architecture.md](../Architecture.md) §14 for the full topic index across all nine
files.

---
### 14.23 Startup/Exit Logging and Fatal Exception Handling

A `Logging` module (`adapters/logging`) wraps spdlog behind `Logging::init/shutdown/log` (Debug/
Info/Warning/Error, timestamped, rotating file sink under `%LOCALAPPDATA%/Exp-LORer/logs/` plus a
console sink in debug builds) — the sole spdlog call site (§6). `main.cpp` logs one line at startup
(after `init()`) and one at clean exit (before `shutdown()`); `ExceptionHandler` logs every fatal
exception (below). Beyond that, call sites span every module under `src/adapters/*` and
`src/adapters/viewmodels` — each failure/operation boundary identified in those modules (scan
start/finish, DB migration, cache hit/miss, decode/checkout/transfer success or failure, etc.) gets
an Info/Debug line on success and a Warning/Error line on failure. Domain/Application are
deliberately not call sites (see the "Not built" paragraph below) — logging is confined to the outer
layers, same posture as `Logging` having no Port of its own.

**Hard rule, enforced at every one of those call sites:** a log line may never contain a specific
file/folder path, filename, or anything derived from one (no `std::filesystem::path`, no
`PathUtf8::toUtf8(path)` output, no `QString::fileName()`, no raw `fs::filesystem_error::what()` /
libgit2 `git_error_last()->message` / libarchive `archive_error_string()` / `QSqlError::text()` text —
all of these embed the actual path(s) involved in the failure). Only `Error.code` (logged via
`toLogString(ErrorCode)`, a small `constexpr` helper next to the enum in `src/domain/Error.h` — kept
there rather than in `adapters/logging` since it's a pure enum-to-name mapping with no Qt/spdlog
dependency) plus counts, durations, byte sizes, and enum values are logged; `Error.message` itself is
never logged. `JsonFileStore` and `ShellContextMenuProvider` previously leaked a path into a log line
each (`qWarning()`/`e.what()`) and were fixed to this same rule as part of establishing it.

Any uncaught C++ exception is caught, logged at Error level, and shown to the user in a modal
dialog (title, exception message, a single "Exit" button) before the process terminates —
`ExceptionHandler::handleFatal` (`src/app`), invoked from two places: a top-level `try/catch`
around `main()`'s body (covers setup before the event loop starts, e.g. `SQLiteTagRepository`
failing to open the DB) and `ExpLorerApplication::notify()` (a thin `QApplication` subclass
overriding `notify()` to catch exceptions thrown during Qt event dispatch — input events, paint
events, queued slot invocations — while `app.exec()` is running).

**Scope:** main-thread exceptions only, consistent with all Port calls still being synchronous on
the UI thread in v1 (§5) — there is no second thread whose exceptions would need separate handling
yet. **Not built:** an `ILogger` Port for Domain/Application to log through (deferred until those
layers actually need to log — `Logging` is plain infrastructure, not wired through the composition
root, so `CompositionRoot`'s own constructor has no dedicated log call either — a construction-time
failure there is still just a thrown exception caught by `main.cpp`'s existing try/catch, already
logged by `ExceptionHandler`); native/SEH crash handling for
non-C++-exception crashes (e.g. access violations); per-thread exception handling for a future
background-dispatch thread (§5's deferred background-dispatch work would need its own try/catch at
each thread's entry point, since exceptions can't cross threads). `ExceptionHandler`/
`ExpLorerApplication` are UI-layer bootstrap code, out of CTest scope per §11 (same posture as the
other UI carve-outs) — smoke-tested manually; `Logging` itself gets an adapter test (`:memory:`-style
real temp directory, per §11) verifying `init()`/`log()`/`shutdown()` produce a log file with the
expected content.

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

### 14.103 Windows Installer (CPack + NSIS, Per-User, No Admin)

A local, opt-in `.\build.ps1 -Package` step produces a Windows installer (`.exe` setup) via CMake's
CPack NSIS generator, installing per-user (no UAC/admin prompt) to
`%LOCALAPPDATA%\Programs\Exp-LORer` — distinct from the app-data location under
`%LOCALAPPDATA%\Exp-LORer` (§9); same root, different subtree, not to be confused. CI integration,
code signing, and auto-update are explicitly out of scope for this pass.

* **Why CPack+NSIS over MSI/WiX or Inno Setup**: CPack is already bundled with the CMake toolchain
  this project requires, needing only `makensis.exe` (NSIS) as one additional local prerequisite —
  the same category as `VCPKG_ROOT`/Visual Studio today, fail-fast-checked by `build.ps1 -Package`
  the same way `cl.exe`/`vswhere` already are. WiX/MSI's per-machine-oriented tooling and Inno
  Setup's non-CMake build step both would have added more friction for less benefit at this stage.
* **Why a patched NSIS template for per-user install**: CMake 4.4's stock
  `Modules/Internal/CPack/NSIS.template.in` hardcodes `RequestExecutionLevel admin`, and separately
  its `.onInit`/`un.onInit` admin-detection logic always resolves to the "AllUsers" branch (every
  code path through `UserInfo::GetAccountType`, including a plain non-admin account, sets
  `$SV_ALLUSERS` to `"AllUsers"`) and calls `SetShellVarContext all`, relying on that
  `RequestExecutionLevel admin` elevation for permission to write to the all-users Start
  Menu/`HKLM`. A straight `admin` → `user` edit alone would leave that logic trying (and, for an
  admin-type Windows account, failing without elevation) to write there. `cmake/packaging/
  NSIS.template.in` is a repo-owned fork of the stock template with: `RequestExecutionLevel user`;
  `.onInit`/`un.onInit`'s admin-detection replaced with an unconditional `"JustMe"` and no
  `SetShellVarContext all` call, so `$INSTDIR` stays at `CPACK_NSIS_INSTALL_ROOT` (set to
  `$LOCALAPPDATA\Programs` in `cmake/packaging/Packaging.cmake`) and every registry/shortcut write
  stays in the current-user context (`HKCU`, per-user Start Menu) for both install and uninstall.
  The template's header comment documents these diffs so a future CMake upgrade's stock template
  can be diffed against it to re-apply them.
* **Packaged contents**: `install()` rules in `src/app/CMakeLists.txt` (co-located with the
  `Exp-LORer` target, matching its own `POST_BUILD` plugin-deployment commands) install the `.exe`,
  its `.pdb` (`OPTIONAL`, so a PDB-less build still packages), and every `*.dll` recursively beneath
  the target's output directory — which already is a fully self-contained, flat deployment tree
  (vcpkg `VCPKG_APPLOCAL_DEPS` plus the existing `platforms/`/`sqldrivers/`/`imageformats/`/
  `multimedia/` plugin-deployment commands), so no manual DLL bookkeeping is needed. The PDB is
  shipped deliberately: `ExceptionHandler` (§14.23) calls `SymInitialize` with a null search path,
  which checks the executable's own directory for a matching `.pdb` to symbolicate fatal-exception
  stack traces — bundling it trades a larger installer for readable crash reports from real
  installs with no separate symbol server.
* **App icon + version resource**: `src/app/resources/AppIcon.ico` (16/32/48/256px; a placeholder
  square glyph pending a real logo — swapping the file later needs no CMake/NSIS change) and
  `src/app/ExpLorer.rc` (icon block + `VERSIONINFO`, versioned from the root `project(... VERSION
  0.1.0 ...)` call via `PROJECT_VERSION_MAJOR`/`MINOR`/`PATCH` compile definitions) give the `.exe`,
  Start Menu shortcut, and installer a non-generic icon plus proper file/product version metadata.
* **Uninstall leaves app data by default**: the default NSIS uninstaller only removes `$INSTDIR`
  (program files) and its own Start Menu/registry entries — matching standard Windows uninstaller
  convention and avoiding silent data loss of `explorer.db`/workspaces/thumbnail cache/logs under
  `%LOCALAPPDATA%\Exp-LORer`. An unchecked-by-default checkbox on a custom uninstall page ("Also
  delete my Exp-LORer settings, tags, and cache", implemented via `nsDialogs` in the patched
  template) opts into an additional `RMDir /r` on that directory.
* **No "app is running" detection**: `WorkspaceInstanceGuard`'s single-instance mutex (§14.60) is
  named from a hash of the resolved data directory plus workspace id, not a fixed/predictable name,
  so neither the installer nor uninstaller can cheaply detect "is Exp-LORer running." Relies instead
  on NSIS's normal locked-`.exe` install/overwrite failure, which already prompts the user to close
  the app and retry — the same expectation `build.ps1`'s own header already sets for building.
* **No shell-integration registry cleanup needed**: the native context menu (§14.102) uses a real
  `IContextMenu` shell popup and never registers `HKCR`/`HKCU\...\shell` keys of its own, so the
  uninstaller has nothing beyond the install directory, Start Menu shortcut, and the installer's own
  uninstall-registry entry to remove.
* **Not built**: CI workflow to build/publish the installer (local-only for this pass); code signing
  (no certificate exists yet — the installer shows the standard "unknown publisher" SmartScreen
  prompt, a follow-up for later, not blocking); an auto-update mechanism; Linux packaging
  (Windows-first per `CLAUDE.md`); MSI/WiX or Inno Setup alternatives.

