# Architecture extension: multi-tab, split-screen, and their composition with tagging/media viewer

## Context

`Architecture.md`/`Specification.md` already define a complete Clean Architecture for file
navigation, SQLite-backed tagging, and an embedded image/video viewer — but neither document
says anything about **multiple tabs** or **split-screen panes**, which the user wants alongside
the existing feature set. The codebase is still genuinely greenfield: only `CompositionRoot`
and `MainWindow` exist as empty stubs, `main.cpp` wires nothing between them, and every layer
under `src/domain`, `src/application`, `src/adapters/*` is an empty `.gitkeep` placeholder. So
this is a pure architecture-definition task — extend the documented design so that when real
code is written, tabs/splits compose correctly with the already-specified navigation/tagging/
media-viewer pieces, without leaking UI-session concepts into the inner layers or breaking the
single-composition-root / single-main-window / no-UI-thread-I/O rules already in place.

Resolved product decisions (confirmed with user, all recommended defaults): tabs contain
independent split-pane trees (not the reverse); the tag filter panel is one shared instance
retargeted to the focused pane; the media viewer is a docked preview pane bound to the active
pane's selection (full-window mode expands it over the whole `MainWindow`); view mode
(tree/grid) is scoped per-pane.

## Key architectural decisions

- **Domain and Application layers are unchanged.** Tabs/Panes/Workspaces are pure UI-session
  state with no business rules — no new entities, no new Ports, no new use cases.
- **`FileNavigationUseCase`, `TagManagementUseCase`, `MediaProcessingUseCase` must be stateless**
  orchestrators over their existing Ports. One shared instance of each serves every pane
  concurrently; only ViewModels are instantiated per-pane.
- **Exactly one `MainWindow`.** Tabs and splits render inside its single central widget; no
  pane/tab code may ever construct a second top-level window (preserves the existing
  single-instance-foreground assumption).
- **Tabs own split-pane trees.** Each `TabViewModel` owns one independent `SplitPaneNode` tree;
  splitting inside a tab never affects other tabs.
- **Tag filter panel and media preview are single shared instances**, retargeted to whichever
  pane currently has focus — not duplicated per pane.

## New/changed components by layer

**`src/domain/`, `src/application/`** — no changes. Existing use cases/Ports are reused as-is,
constrained to stay stateless so CompositionRoot can safely share one instance of each across N
panes.

**`src/adapters/viewmodels/`** (new files)
- `NavigationHistory.h/.cpp` — per-pane back/forward stack (Command pattern: `NavigateToPathCommand` objects pushed/popped). Plain C++, no Qt.
- `SplitPaneNode.h/.cpp` — Composite: abstract `SplitPaneNode`; leaf `PaneNode` (holds a `PaneId`); composite `SplitContainerNode` (orientation + ordered children + size ratios). Plain C++ tree, no `QWidget`/`QObject` — testable/serializable independent of the widget tree.
- `PaneViewModel.h/.cpp` — `QObject`; owns one `FileTreeViewModel`/`FileGridViewModel` + one `NavigationHistory` + a `PaneId` + its own view-mode state; exposes `navigateTo/goBack/goForward/goUp`; emits `directoryChanged`, `activated`, `selectionChanged`.
- `TabViewModel.h/.cpp` — `QObject`; owns the root `SplitPaneNode` for one tab, a title, and `activePaneId`; emits `titleChanged`, `layoutChanged`, `activePaneChanged`.
- `WorkspaceController.h/.cpp` — the Mediator. Owns the ordered `TabViewModel` list + active-tab index; exposes `addTab()/closeTab()/splitPane()/closePane()/activePane()`; retargets the shared `TagListViewModel`/`MediaPreviewViewModel` to the active pane on focus change; produces/consumes a `WorkspaceLayoutSnapshot`.
- `WorkspaceLayoutSnapshot.h` — plain struct (Memento): recursive split shape + each leaf's path/view-mode + active tab/pane indices. Uses `std::filesystem::path`, no Qt types — QSettings/JSON conversion happens only at the adapter boundary that persists it.

**`src/adapters/persistence/`** (new file)
- `WorkspaceSessionStore.h/.cpp` — reads/writes `WorkspaceLayoutSnapshot` via `QSettings`, mirroring the existing window-geometry handling (Architecture.md §9). Deliberately **not** a new Port/use case: it's UI-session persistence, not business data, so it doesn't go through `ITagRepository`/SQLite.

**`src/ui/widgets/`**
- `MainWindow.h/.cpp` (modified) — hosts a tab strip (`QTabWidget`) of `SplitPaneWidget`s built from a `WorkspaceController` handed in by `CompositionRoot`; gains New Tab/Close Tab/Split/Close-Pane actions; hosts the docked media-preview widget and `TagFilterPanel`, each bound to the one shared ViewModel instance.
- `SplitPaneWidget.h/.cpp` (new) — recursively builds nested `QSplitter`s from a `TabViewModel`'s `SplitPaneNode` tree.
- `FileBrowserPaneWidget.h/.cpp` (new) — leaf widget: `QTreeView`/`QListView` bound to its `PaneViewModel`'s `FileTreeViewModel`/`FileGridViewModel`, plus a per-pane breadcrumb/back/forward toolbar.
- `TagFilterPanel` — existing planned type, unchanged; single dock widget bound to the one shared `TagListViewModel`.

**`src/app/CompositionRoot.h/.cpp`** (modified)
- Keeps single shared adapter instances (`StandardFileSystemRepository`, `SQLiteTagRepository`, media decoder, shared thread pool) and single shared use-case instances.
- Gains Factory Methods: `createPaneViewModel(viewMode)`, `createTabViewModel()`, `createWorkspaceController()` — each wires a fresh ViewModel to the shared use cases. No DI framework/Abstract Factory introduced — stays consistent with "single composition root, constructor injection only."

## Component relationship

```
MainWindow (single top-level window)
 └── WorkspaceController (Mediator)
      ├── TabViewModel #1
      │     └── SplitPaneNode tree
      │           └── SplitContainerNode
      │                 ├── PaneNode → PaneViewModel A → FileTreeViewModel A ─┐
      │                 └── PaneNode → PaneViewModel B → FileGridViewModel B ─┤
      │           (each PaneViewModel owns its own NavigationHistory)         │
      ├── TabViewModel #2 (single unsplit PaneNode)                          │
      ├── TagListViewModel (shared, 1 instance) ◄── retargeted to active pane
      └── MediaPreviewViewModel (shared, 1 instance) ◄── retargeted to active pane
                                                                              │
                                                                              ▼
                                                   FileNavigationUseCase (shared, stateless)
                                                                              │
                                                                              ▼
                                                   IFileSystemRepository (shared, CompositionRoot)
                                                   → shared thread pool → queued signal back to
                                                     the originating FileTreeViewModel/Grid only
```

## SOLID / pattern rationale

- **SRP**: `PaneViewModel` (display/query orchestration) vs. `NavigationHistory` (where you've
  been) vs. `TabViewModel` (tab metadata + active-pane bookkeeping) vs. `WorkspaceController`
  (cross-pane/tab mediation) are four distinct reasons to change.
- **OCP**: new pane flavors are added via new `CompositionRoot` factory methods, not by widening
  `PaneViewModel`'s constructor contract.
- **LSP**: `PaneNode`/`SplitContainerNode` are interchangeable to any tree-walker (renderer,
  serializer) — required for the Composite to work.
- **ISP**: `IFileSystemRepository`/`ITagRepository`/`IMediaDecoder` get no pane-aware methods —
  panes are presentation-only and must not leak into Ports built around `FileNode`/`Tag`.
- **DIP**: `PaneViewModel` never references a concrete sibling `PaneViewModel` or
  `WorkspaceController` directly; it only emits Qt signals that `WorkspaceController` subscribes
  to (Observer realizing the Mediator).

**Patterns**: Composite (`SplitPaneNode` tree), Factory Method (`CompositionRoot::createXViewModel()`),
Mediator (`WorkspaceController`), Observer (Qt signals/slots, the codebase's existing idiom),
Command (`NavigationHistory`'s back/forward stacks), Memento (`WorkspaceLayoutSnapshot`).

## Usage rules

**Must:**
- `FileNavigationUseCase`/`TagManagementUseCase`/`MediaProcessingUseCase` stay stateless so one
  shared instance safely serves concurrent calls from N panes.
- Cross-pane/tab coordination goes through `WorkspaceController`; `PaneViewModel` communicates
  only via its own signals.
- `PaneViewModel`/`TabViewModel`/`WorkspaceController` are created only via `CompositionRoot`'s
  factory methods — UI widgets request new panes/tabs through `WorkspaceController`'s API, never
  `new PaneViewModel(...)` directly.
- `SplitPaneNode` and `WorkspaceLayoutSnapshot` stay plain C++ (no `QWidget`/`QObject` embedded);
  the `QSplitter` tree is built *from* them, separately.
- All async pane work flows through the existing shared repository/decoder instances and their
  shared thread pool — no ad hoc per-pane threads.
- `WorkspaceLayoutSnapshot` persistence goes through `QSettings` only, never `ITagRepository`/SQLite.
- Exactly one `MainWindow` per process; tabs/splits render only inside it.
- Focus changes (pane activation) retarget the shared `TagListViewModel`/`MediaPreviewViewModel`
  through `WorkspaceController` — no pane holds its own copy of either.

**Must not:**
- No `PaneId`/`TabId`/`SplitPaneNode` type may appear in any `src/domain` or `src/application`
  header.
- No pane/tab/workspace component may create its own `QThread`/`QThreadPool`/`std::thread`.
- `NavigationHistory` must not perform disk I/O itself — only bookkeeping; validity is re-checked
  when `navigateTo` re-runs through `FileNavigationUseCase`.

## Navigation history, session persistence, threading

- **History is per-pane**: each `PaneViewModel` owns exactly one `NavigationHistory`; closing a
  pane discards it; there is no global back/forward.
- **Session persistence** plugs in at `WorkspaceSessionStore` (QSettings-backed): captures tab
  titles + split shape + each leaf's current path + view mode; restored at startup by replaying
  `WorkspaceController::restore(snapshot)`, which calls `CompositionRoot::createPaneViewModel()`
  per leaf then `navigateTo(path)`. Back/forward history stacks are **not** persisted in v1
  (rebuilt empty on restore) — deliberate minimalism.
- **Threading**: simultaneous navigations from different panes become independent tasks on the
  same shared `IFileSystemRepository`-owned pool; results marshal back via queued signals to the
  *originating* `FileTreeViewModel`/`Grid` only (each is a distinct `QObject`) — no cross-pane
  bleed, no per-pane thread pools.

## Deferred UX policy (not architecture-blocking, default stated for now)

- Max pane count / split nesting depth: architecturally unbounded (Composite recurses); suggest
  a UX cap (~4–6 panes, depth 2) to be enforced in `WorkspaceController::splitPane()`, not in
  `SplitPaneNode` itself.
- Closing the last tab: disable "Close Tab" when exactly one tab remains, rather than closing
  the window or showing an empty state.
- Cross-pane drag-and-drop (dragging a file between panes to copy/move): out of scope for this
  extension; if added later, `WorkspaceController` is the natural broker — not assumed here.

## Deliverable for this task

Write the above into `Architecture.md` as a new section (e.g. "§14 Multi-Tab / Split-Screen
Extension"), and update:
- The "Main Components Overview" table (§4) to include `WorkspaceController`, `TabViewModel`,
  `PaneViewModel`, `SplitPaneNode`.
- The directory-structure listing (§10) to list the new files under
  `src/adapters/viewmodels/`, `src/adapters/persistence/`, and `src/ui/widgets/`.
- `Specification.md` §2.1 (Directory Navigation) with a short functional-requirements note for
  multi-tab + split-screen, mirroring the style of the existing tagging (§2.2) and viewer
  (§2.3/§2.4) sections.

No source code is written in this task — `src/domain`/`src/application`/`src/adapters/*` stay as
`.gitkeep` placeholders per the project's current scaffolding stage; this only updates the design
docs that a future implementation task will follow, consistent with CLAUDE.md's instruction to
read `Specification.md`/`Architecture.md` before implementing a new component.

## Verification

- Proofread the new `Architecture.md` section against the existing document's terminology/style
  (layer names, `I`-prefix convention, `PascalCase`/`camelCase`, "must/must not" phrasing already
  used in §§2, 5, 6, 9, 11, 12) so it reads as part of the same document, not a bolted-on appendix.
- Confirm no new entity/Port/use-case was introduced in the domain/application sections (the
  extension should only add adapter/UI/app-layer content).
- Cross-check the updated §4 table and §10 directory listing stay consistent with the prose
  sections (no component named in one place and missing from the other).
