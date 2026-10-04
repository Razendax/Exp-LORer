# UI Tests — Current Coverage

This document describes, in plain language, what the `ui_tests/` black-box suite actually
exercises today. It is a snapshot of test *intent and behavior*, not a reproduction of the code —
read the referenced test files under `ui_tests/tests/` for exact assertions.

See `ui_tests_gaps.md` (sibling doc) for what this suite does **not** cover yet.

## What this suite is

`ui_tests/` is a Python/pywinauto suite that drives the real, compiled `Exp-LORer.exe` through
Windows UI Automation — it never calls into C++ code directly. It is **opt-in**: it is not part of
`build.ps1` or CTest, has its own virtualenv (`ui_tests/setup_env.ps1`), and a developer runs it by
hand (`ui_tests/run_ui_tests.ps1`) against an already-built app.

Three test-execution styles are used, chosen per suite based on how much state must be isolated:

- **Fresh instance per test method** (`app`/`start_app` fixtures, or the `ExpLorerFreshAppTestCase`
  base class) — used whenever a test's assertions depend on *global* state (tags, favorites, column
  defaults) that would otherwise leak between test methods in the same class.
- **One shared instance per test class** (`ExpLorerTestCase`) — used when tests only touch their own
  fresh `tmp_path` folder, to amortize the ~1s app-launch cost.
- **Restartable instance** (`ExpLorerRestartableTestCase`) — closes and relaunches the same instance
  against the same data directory mid-test, to prove that a save-on-close/restore-on-launch round
  trip actually works through a real process lifecycle, not just that the JSON shape is correct.

Non-ASCII (Cyrillic/Japanese/emoji) file, folder, tag, and favorite names are deliberately exercised
throughout the suite wherever relevant, since this is a hard requirement of the app
(`Specification.md` line 34) and a narrow string conversion bug would crash the whole process.

## Covered functionality, by area

### Application startup
The app launches, its main window appears, is visible, and is titled "Exp-LORer"
(`test_smoke.py`).

### Layout switching and multi-pane
Switching between single-pane, two-pane (vertical split), and four-pane grid layouts via the
layout toolbar correctly shows/hides each pane's file browser (`test_framework_demo.py`,
`test_lifecycle_and_config_demo.py`). A two-pane layout restored from a saved config starts each
pane on its own configured folder (`test_config_load.py`).

### Navigation (back / forward / up)
Per-pane Back/Forward/Up controls are covered in detail (`test_navigation.py`):
- Back/Forward are disabled with no history, and enable/disable correctly as the user moves through
  folders.
- Navigating to a new folder after going back clears forward history.
- Repeated Back clicks walk all the way back to the virtual "This PC" root.
- Up navigates to the parent folder, is disabled at the "This PC" root, and an Up navigation is
  itself recorded in Back history.

### Tabs
Opening new tabs, tab titles reflecting the browsed folder, switching tabs restoring each tab's own
location, one tab's navigation not affecting another, and — importantly — each tab keeping its own
independent Back/Forward history are all covered (`test_tabs.py`).

### Details view (file/folder listing)
The Details view (the default QTreeView-based listing) is checked for:
- Correctly listing files and folders with the right Type (extension-derived, "File folder" for
  directories, generic "File" for extensionless files) and Size (blank for directories, raw byte
  count for files), plus a sane date format (`test_details_view.py`).
- Correctly showing an empty folder as zero rows.
- Live, automatic updates when files/folders are added, deleted, or modified on disk — driven by the
  background directory watcher, with no manual refresh — covering new files, deleted files, growing
  file sizes, and new subfolders (`test_live_update.py`).

### Sorting
Column-header click-to-sort is covered for the Size column, confirming re-sorting actually
happens (not just that the result happens to already be sorted), that clicking the same header
twice reverses direction, and that the active sort criterion/direction persists when the tab
navigates to a different folder without an explicit re-click (`test_details_view_sorting.py`). The
Folder Size feature's Percent column is sorted the same way (`test_folder_size.py`).

### Customizable Details-view columns
This is one of the more thoroughly covered features (`test_column_customization.py`):
- Every toggleable column (Size/Type/Date modified/Git/Tags/Date created/Percent) is visible by
  default.
- Unchecking/rechecking a column in the header's right-click menu hides/reshows it, per tab.
- A new tab's default column visibility honors a persisted `column_visibility.json` default.
- Hiding a column in one tab leaves a sibling tab (and a tab created after a restart) on the normal
  default — proven across an actual app restart, not just in the live session.
- Dragging a column header reorders the visible columns, and that reordering survives an app
  restart.

### Session persistence (config.json)
Both halves of the save/restore cycle are covered:
- **Loading** a hand-seeded `config.json` on startup correctly restores: multiple tabs per pane with
  the right active tab selected, a two-pane layout with each pane on its own distinct folder, Details
  vs. non-Details (icon) view mode, a non-ASCII folder path, and Details-view column widths
  (`test_config_load.py`).
- **Saving on close** correctly persists: the current folder/layout/sort criterion after navigating
  and clicking a different layout, a toggled (reversed) sort direction, multiple tabs and which one
  is active, and that untouched column widths round-trip unchanged (`test_config_save_on_close.py`).

### Left side panel — folder tree
The collapsible left panel's "Tree" tab (a folders-only tree) is covered for: the panel starting
collapsed and the toggle button expanding/collapsing it; the tree automatically revealing and
selecting the active tab's current folder, including when the panel is expanded *after* the fact
and when switching between tabs; showing folders but filtering out plain files; single-click
navigating the active tab; and inline F2 rename (which also moves the active tab along if it was
browsing the renamed folder) (`test_left_panel_tree.py`).

### Left side panel — quick search toggles
The Search tab's Match Case / Whole Word / Exact Name / Match Regex toggle buttons are covered
(`test_search_match_options.py`): Match Case narrowing and widening results for a differently-cased
query, and the mutually-exclusive-but-optional behavior of Whole Word/Exact Name/Regex — clicking
an already-active one again reverts to plain "Contains" matching.

### Right side panel — Tags
The Tag Panel (apply/create/remove tags on the current selection) has the deepest coverage of any
single feature (`test_tag_panel.py`), including:
- Creating and applying a brand-new tag to a selected file, or to the browsed folder itself when
  nothing is selected.
- Applying an *existing* tag found via the panel's own search-matches list.
- Tags created from one location being visible/searchable from a completely different folder
  (proving tags are global, shared state, same repository the Tag Manager dialog uses).
- The "create tag" affordance correctly hiding itself once an exact (case-insensitive) match already
  exists.
- Multi-selection and the virtual "This PC" root both being tagging no-ops that don't crash.
- A folder's own tags, and an ancestor folder's tags, both showing up while browsing into/through
  them.
- Switching selection between two differently-tagged files updating the "tags on selection" section
  correctly.
- Removing a tag via its chip's × button, including that removing it from one file leaves it intact
  on another, and that un-tagging a file does **not** delete the tag itself from the global list.
- Tags on both a file and a folder surviving a full app restart (via two separate app instances
  against the same data directory).
- Non-ASCII tag names and non-ASCII target names round-tripping correctly throughout.

### Tag Manager dialog
The standalone global tag-management dialog (Edit > Tag Edit...) is covered end-to-end
(`test_tag_manager_dialog.py`): adding a tag, name trimming, rejecting an empty/cancelled add,
rejecting an exact-duplicate name while *allowing* a differently-cased name (case-sensitive
uniqueness), renaming (including trim, same-name no-op, rejecting a collision, and cancel),
deleting with/without confirmation, non-ASCII names, button enablement tied to chip selection, the
search box filtering the chip list, and the close button.

### Favorites
Both ways of managing favorites are covered:
- **Adding via the Favorites menu** (`test_favorites_menu.py`): the action's enabled state (disabled
  at "This PC", enabled once browsing a real folder), adding the browsed folder when nothing is
  selected, adding a single selected file, adding each of several selected items as its own
  favorite, idempotent re-adding of the same path, and a non-ASCII folder name round-tripping.
- **The standalone Manage Favorites dialog** (`test_manage_favorites_dialog.py`): adding categories
  (with name trimming), renaming and removing categories, button-enablement rules tied to selection
  (including that a plain item can't be renamed but can be filed into a category, while a category
  can't itself be moved into another category), reordering entries up/down with correct
  first/last-entry disabling, removing a single entry without disturbing others, and non-ASCII
  category names. (Favorites are seeded via the menu action rather than the dialog's own native
  Add File/Add Folder pickers — see the gaps doc.)

### Folder Size (disk usage)
Covered for the "Scan Folder" path only (`test_folder_size.py`): the scan button being disabled
while browsing the virtual "This PC" root; a subfolder's Size column staying blank until scanned;
a scan populating that size (human-readable formatted) after the view refreshes; the scan button
re-enabling and reverting its label once the background scan finishes; and the Percent column
correctly showing each entry's share of the current folder's total, sortable the same way other
columns are.

### Left/right panel state persistence across restart
Covered end-to-end via real close/relaunch cycles (`test_side_panel_persistence.py`): which tab
(Tree/Search for the left panel, Tags/Preview/Monitoring for the right) was active and its expanded
width both survive a restart; a collapsed panel stays collapsed and its width resets to the "unset"
sentinel; width stays stable across *multiple* consecutive restarts with no drift; and a regression
test specifically confirms that maximizing the window before closing does not spuriously re-expand
panels the user had deliberately collapsed.

### Framework self-demonstration
Two files (`test_framework_demo.py`, `test_lifecycle_and_config_demo.py`) are not feature coverage
in themselves — they demonstrate the page-object framework's own chained syntax and the
shared-instance `ExpLorerTestCase` pattern. They incidentally touch layout switching, navigation
toolbar state, and right-panel tab switching (just existence/text checks, not real Preview/
Monitoring behavior).
