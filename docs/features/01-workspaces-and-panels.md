# Architecture Document — Feature Decision Log: Workspaces & Panels

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.1–14.8, §14.14, §14.24–14.25, §14.41, §14.48, §14.60 — split-window layout/panes/tabs,
session persistence, the bottom/right/left collapsible panel shells (and their first tab each:
Terminal, Tags, folder Tree), left/right panel state persistence, and named workspaces
(multi-instance launch). Section numbers are **not** contiguous with this file's neighbors — they
were assigned chronologically as each feature was built and are kept stable since code comments
cite them directly. See [Architecture.md](../Architecture.md) §14 for the full topic index across
all nine files.

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

### 14.24 Bottom Panel and Integrated Terminal

A VS Code-style collapsible panel at the bottom of every tab's own page (this app's tab, i.e. one
page of a pane's `QTabWidget` — not a top-level window), showing a strip of "panel tabs"; only
"Terminal" exists in v1, built so more (e.g. Problems/Output) can be added later without new
plumbing. Collapsed by default; the first time a tab's Terminal panel tab is clicked, a default
`cmd.exe` session is lazily spawned in that tab's current folder at that moment — it does not
follow later navigation in the same tab. A right-side list inside the Terminal panel tab (see
`TerminalSessionsWidget` below) lets the user open further sessions, of either shell, switch
between them, and close any one independently; collapsing/re-expanding the panel just hides/shows
all still-running sessions. Closing the tab kills every one of its sessions' processes.

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
  `WorkspaceLayoutTopology`. `TerminalShellType` — a plain `Cmd`/`PowerShell` enum (plus a
  `terminalShellDisplayName()` helper) shared between the UI's shell-picker and
  `WindowsConPtyProcess`; no abstraction beyond that (not a Port — see "No Application-layer Port"
  above). `WindowsConPtyProcess` (Windows-only, same "Windows first" posture as
  `ShellContextMenuProvider`) — a `QObject` wrapping ConPTY (`CreatePseudoConsole` + `CreateProcessW`);
  `start()` takes a `TerminalShellType` and resolves the command line accordingly — `Cmd` reads
  `%ComSpec%` (falling back to `cmd.exe`), `PowerShell` passes the literal `powershell.exe` and lets
  `CreateProcessW`'s standard search order resolve it off `PATH` (Windows PowerShell ships with
  Windows and is on `PATH` by default — no `ComSpec`-equivalent env var needed; this is Windows
  PowerShell, not PowerShell 7's `pwsh.exe`, which isn't guaranteed installed). Runs one background
  `std::thread` blocking on `ReadFile` of the output pipe, posting each chunk back to the UI thread
  via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` as `dataReceived(QByteArray)` — the only
  cross-thread hop, keeping ConPTY's blocking I/O off the UI thread (§5); emits `processExited(int)`
  when the pipe closes. `writeInput`/`resize` are synchronous UI-thread calls (`WriteFile`/
  `ResizePseudoConsole`), the same accepted posture as `ShellExecuteW` (§14.11). No `IPtyProcess`
  interface: Qt doesn't support pure-virtual signals, and nothing outside the UI layer needs to mock
  this (§11 already excludes UI from CTest) — a future Linux `PosixPtyProcess` is deferred until
  Linux support is actually built, at which point an interface can be introduced alongside it.
* **`src/ui/widgets`**: `TerminalWidget` — owns one `AnsiTerminalBuffer` + one `WindowsConPtyProcess`
  for a single shell session (its `TerminalShellType`, fixed at construction and reused on
  restart), paints the grid with a monospace font, translates key events to VT byte sequences, and
  resizes the pty on `resizeEvent`. On process exit, shows an inline restart prompt rather than
  auto-restarting. It also owns keyboard-only text selection and clipboard state — an anchor/active
  pair of `{logicalRow, column}` (logical row spanning scrollback + live grid, the same indexing
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
  `TerminalSessionsWidget` — the Terminal panel tab's actual content (replacing a bare
  `TerminalWidget`): a `QStackedWidget` of per-session `TerminalWidget`s (plus a permanent
  empty-state page shown when no session is open) next to a fixed-width right-side column listing
  every open session. The column's `+` button (`terminalAddSessionButton`) pops an on-demand `QMenu`
  (same convention as `StatusBarWidget::showBranchMenu`) offering "Command Prompt"/"PowerShell";
  choosing one creates and activates a new session. Each session gets one row (a select button
  showing its label, e.g. "cmd 1"/"PowerShell 1" — a per-shell-type counter that only increments,
  never renumbering existing rows — plus a close button styled like `SearchCriteriaPanel`'s, `"×"`
  on `UiColors::closeButtonRed`/`closeButtonRedHover`); clicking the select button switches the
  stack to that session, clicking close tears its process down and removes the row, falling back to
  another remaining session or the empty-state page. Exposes `activateDefaultSession()` (creates the
  first, `cmd`, session — called from `panelTabFirstActivated`, below) and `sessionCreated
  (TerminalWidget*)` (fired for every session, first or later, so the owner can `start()` it with a
  concrete cwd).
  `BottomPanelWidget` — the generic collapsible panel shell: a header strip of checkable panel-tab
  buttons over a `QStackedWidget` of panel-tab content, starting collapsed; emits `expansionChanged`
  (for the owning splitter to resize) and `panelTabFirstActivated` (for lazy terminal start), staying
  itself splitter-agnostic. `WorkspacePaneWidget::addPageForTab` wraps its existing per-tab
  browse/search/advanced-search `QStackedWidget` and a `BottomPanelWidget` in a vertical `QSplitter`
  as the actual tab page; `onViewModeChanged`/`currentColumnWidths` unwrap `splitter->widget(0)`
  instead of assuming the tab page is the `QStackedWidget` directly. The `panelTabFirstActivated`
  connection is scoped to the Terminal panel tab's own registered index (from `addPanelTab`'s return
  value) before calling `activateDefaultSession()` — needed now that the signal can arrive for any
  panel tab's first activation, not just Terminal's.
* **Not built:** a Linux backend; a shell/command picker beyond cmd and PowerShell (e.g. WSL,
  PowerShell 7, arbitrary executables); mouse drag-to-select (selection is keyboard-only, see
  above); 256-color/truecolor; persisting the open-sessions list, active session, chosen shells, or
  panel open/closed state/size across restart (no `WorkspaceConfig`/§14.14 involvement — resets
  every launch); a session's directory following subsequent navigation in its tab (only the
  directory at creation time). No automated coverage beyond `AnsiTerminalBuffer` — ConPTY spawning
  and the painting/input/selection/clipboard/session-list widget code fall under the existing §11
  UI/OS-shell carve-out, smoke-tested manually.

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

*Superseded in part by §14.41:* `RightPanelWidget` was generalized into `SidePanelWidget(Edge)`,
which now serves both this panel and the new left panel; its behavior here is unchanged.

*Superseded in part by §14.48:* this panel's expanded/collapsed state, active tab, and width are
now persisted across restarts, alongside the left panel's.

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

