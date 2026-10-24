# Architecture Document — Feature Decision Log: Keyboard Hotkeys & Navigation

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.108 — the customizable, app-wide keyboard hotkey catalog (navigation, tabs, panes,
layout, panels, search/tools, and the file-operations hotkeys originally described in §14.10,
now folded into the same mechanism) and its Settings > General > Hotkeys page.

---
### 14.108 Customizable Hotkey Catalog

Every rebindable keyboard action in the app — including §14.10's previously-fixed clipboard/
delete/rename/parent-folder/open keys — is now one row in a single catalog
(`src/adapters/hotkeys/HotkeyCatalog`), persisted as user overrides in `hotkeys.json` (only the
bindings that differ from the shipped default are written, keyed by a stable per-action string id,
not the `HotkeyAction` enum value — so adding/renaming actions in a future version never requires
migrating the file). Same tolerant-load/atomic-save shape as `favorites.json`/
`environment_variables.json` (§9), shared across every workspace rather than per-workspace, since
a user's key bindings are a personal preference.

* **Catalog entry** (`HotkeyCatalogEntry`): action id, stable JSON key, category (matching the
  Settings page's six group headers), display name, default chord, **scope** (`View`/`Window`),
  and whether a bare (no-modifier) rebind is allowed for that one action (true only for the small
  set of actions whose own shipped default is itself a bare key or Shift+key: Rename, Refresh,
  Cycle-focus-next-pane, Delete, Delete-permanently, Navigate-to-parent, Open/navigate-into-
  selection, Properties).
* **Scoping, two dispatch paths, no `QAction::setShortcut`/`QShortcut`:** Qt's own ambiguous-
  shortcut handling fires *neither* action when two `QShortcut`/`QAction`s share an identical
  context+chord, which would contradict this feature's "conflicts are allowed; whichever was bound
  most recently wins" rule (chosen over blocking conflicts outright, so a settings page warning —
  not a hard stop — is enough). So neither dispatch path uses a Qt-native shortcut object:
  * **View-scoped** (needs file-browser-view focus — most of Navigation plus every
    file-operation/selection action): `FileBrowserView::handleKeyPress` resolves the pressed
    `QKeySequence` against `HotkeyManager::resolve(Scope::View, chord)` instead of hardcoded
    `Qt::Key_X` checks, same event-filter-on-the-two-child-views scoping §14.10 originally
    established.
  * **Window-scoped** (fires regardless of which widget has focus — Tabs/Panes/Layout/Panels/
    Search & Tools, since these are quick jumps *to* a focus target, not actions already assuming
    one): a single `HotkeyWindowDispatcher` installed as a `QApplication`-wide event filter on
    `QEvent::ShortcutOverride` (claiming it before a focused text field would otherwise swallow
    it) invokes a `std::function<void()>` callback registered once per action — the same lambda
    bodies already connected to each action's menu/toolbar `QAction::triggered`, so the dispatcher
    is a second trigger path for existing code, never a second implementation of it.
* **Same-scope duplicate bindings:** `HotkeyBindings::Override` carries a monotonic `order`
  counter, bumped and persisted on every rebind, so "most recently bound wins" is deterministic
  and survives a restart. `HotkeyManager::resolve(scope, chord)` picks the highest-`order` action
  among every action in that scope currently bound to the pressed chord.
* **Settings page** (`src/ui/widgets/core/SettingsHotkeysPage`, a sixth `General` sub-page
  alongside UI/Colors/Notifications/Columns/Preview): a `QTreeWidget` (collapsible category
  headers for free) with a search/filter box above it and a small custom `HotkeyEditWidget` per
  row for capturing a new chord ("listening" mode, Escape cancels, a chord without a modifier is
  rejected unless that row's `allowsBareRebind` is set). Per-row Clear/Reset plus one
  "Restore All Defaults" (confirmed) for the whole table. Edits apply and persist immediately, same
  posture as every other Settings page.
* **Unbound action:** clearing a shortcut never disables the action — every action already has a
  menu/toolbar/context-menu entry, except the brand-new ones this feature introduces (most of
  Navigation/Tabs/Panes/Layout/Panels), which gain a menu entry alongside their shortcut as part of
  this feature, consistent with every other action in the app.
