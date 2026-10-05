# UI Tests — Current Coverage

This document describes, in plain language, what the `ui_tests/` black-box suite actually
exercises today. It is a snapshot of test *intent and behavior*, not a reproduction of the code —
read the referenced test files under `ui_tests/tests/` for exact assertions.

See `UiTestsGaps.md` (sibling doc) for what this suite does **not** cover yet.

## What this suite is

`ui_tests/` is a Python/pywinauto suite that drives the real, compiled `Exp-LORer.exe` through
Windows UI Automation — it never calls into C++ code directly. It is **opt-in**: it is not part of
`build.ps1` or CTest, has its own virtualenv (`ui_tests/setup_env.ps1`), and a developer runs it by
hand (`ui_tests/run_ui_tests.ps1`) against an already-built app.

Three test-execution styles are used, chosen per suite based on how much state must be isolated:

- **Fresh instance per test method** (`app`/`start_app` fixtures, or the `ExpLorerFreshAppTestCase`
  base class) — used whenever a test's assertions depend on *global* state (tags, favorites, column
  defaults, window-wide search/address-bar text) that would otherwise leak between test methods in
  the same class.
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

### "This PC" virtual navigation root
Dedicated coverage (`test_this_pc.py`, Architecture.md §14.15), on top of the incidental use of
"This PC" as a starting/ending point throughout the rest of the suite:
- Navigating Up from a drive's root lands on "This PC" and lists exactly the set of drive letters
  actually present on the machine (cross-checked against `os.path.exists` for each letter), with Up
  disabled there.
- Double-clicking a drive row from "This PC" navigates into that drive's root.

### Address bar autocomplete
Covered (`test_address_bar_autocomplete.py`, `AddressBarWidget`, Architecture.md §14.21):
- An unambiguous partial path inline-completes (ghost-completion) to the one matching folder.
- A partial path with no match leaves the typed text unchanged.
- Pressing Enter after an inline completion navigates to the completed folder.
- Non-ASCII (Cyrillic) folder names autocomplete the same way.

The popup's own suggestion list (as opposed to the inline ghost-completion in the address bar
itself) is not asserted on — see `UiTestsGaps.md` for why.

### Tabs
Opening new tabs, tab titles reflecting the browsed folder, switching tabs restoring each tab's own
location, one tab's navigation not affecting another, and — importantly — each tab keeping its own
independent Back/Forward history are all covered (`test_tabs.py`).

### Details view (file/folder listing)
The Details view (the default QTreeView-based listing) is checked for:
- Correctly listing files and folders with the right Type (extension-derived, "File folder" for
  directories, generic "File" for extensionless files) and a human-readable Size
  (`FileSizeFormat::format`, e.g. "5 B"/"1.2 KB" — blank for directories), plus a sane date format
  (`test_details_view.py`).
- Correctly showing an empty folder as zero rows.
- Live, automatic updates when files/folders are added, deleted, or modified on disk — driven by the
  background directory watcher, with no manual refresh — covering new files, deleted files, growing
  file sizes, and new subfolders (`test_live_update.py`).
- The watcher's scope boundary: a change made several levels down inside a watched folder's
  subfolder does **not**, by itself, refresh that folder's own listing — the watch is non-recursive
  (Architecture.md §14.33) — confirmed by waiting past any plausible callback window and then
  showing an explicit refresh still returns the same (unchanged) listing, so the check isn't a false
  negative from reading too early (`test_live_update.py`).
- Catch-up on background tabs: a second, currently-backgrounded tab in the same pane is not
  live-updated while inactive, but switching to it re-evaluates the watched-tab set and immediately
  refreshes it to show whatever changed while it was backgrounded, with no manual refresh needed
  (`test_live_update.py`).

### In-view rename and delete
F2 rename and Delete/Shift+Delete, driven directly against the main Details view (not the left
panel's folder tree), are covered end-to-end (`test_details_view_rename_delete.py`,
`FileBrowserView::handleKeyPress`, `FileNameEditDelegate`, Architecture.md §14.13's gap note):
- F2 + typing + Enter commits a rename; Escape cancels it, leaving the file untouched.
- An empty new name, and a name colliding with an existing sibling, are both rejected (the original
  file/name survives unchanged).
- Delete moves a file to the recycle bin after confirmation; Shift+Delete permanently deletes after
  a (differently worded) confirmation; declining either confirmation leaves the file untouched.
- Deleting the folder a *different*, currently-inactive tab is browsing navigates that tab up to the
  parent gracefully (no crash, no dangling state) once it becomes active again.
- Non-ASCII names (including emoji in the pre-existing name) round-trip through both rename and
  delete.

The same entry points reachable via the right-click context menu's "Rename"/"Delete" are covered
separately below rather than here.

### Multi-selection
Dedicated coverage of selection mechanics in the Details view (`test_multi_selection.py`,
Architecture.md §14.16, §14.31's `StatusBarWidget::setCounts`):
- Shift-click selects a contiguous range; Ctrl-click adds/removes individual rows from the
  selection (including toggling an already-selected row back off).
- Ctrl+A selects every row in the current folder.
- The per-pane status bar's counts label reflects the selection count live (`"N items"` vs.
  `"N items, M selected"`) as the selection changes.
- The per-pane status bar's quick-select text box selects every row whose name contains the typed
  text.

This complements the older incidental multi-selection coverage in `test_tag_panel.py`/
`test_favorites_menu.py` (proving a multi-selection is a tagging/favoriting no-op), which still
stands on its own.

### Opening files with the default application
Covered (`test_open_with_default_app.py`, `FileOperationsController::openFile` →
`FileNavigationUseCase::openFile`, Architecture.md §14.11):
- Double-clicking a file launches its OS-registered default application (asserted loosely, as "some
  new top-level window appeared", relying on `.txt` being Notepad-registered on stock Windows —
  this suite has no page object for arbitrary external editors and isn't meant to grow one).
- Pressing Enter on a selected file does the same.
- Double-clicking a folder navigates into it instead of launching anything.

The test file's own docstring notes these two file-opening tests could not be verified passing
inside this repository's own sandboxed dev session (the OS file-association broker appears to hang
there independent of this app) — a real interactive Windows session shouldn't hit this.

### Keyboard clipboard operations (Cut / Copy / Paste)
Covered (`test_clipboard_operations.py`, `FileBrowserView::handleKeyPress`'s Ctrl+C/Ctrl+X/Ctrl+V →
`FileOperationsController::copyToClipboard`/`cutToClipboard`/`pasteInto`, Architecture.md §14.10):
- Ctrl+C then Ctrl+V copies a file, leaving the original in place.
- Ctrl+X then Ctrl+V moves (cuts) a file.
- Both operations work the same over a multi-selection, transferring every selected item.
- Pasting with nothing copied/cut (an empty or externally-cleared OS clipboard) is a no-op — no
  crash, and the app stays responsive.
- Pasting a cut item back into the folder it came from is a no-op that leaves it untouched.
- A non-ASCII filename (Cyrillic/Japanese/emoji) round-trips correctly through the clipboard.

These tests drive the real Windows clipboard (via `win32clipboard`), each in its own fresh app
instance so a leftover clipboard state can't make another test flaky.

### Custom context menu
Covered for the app's own entries (`test_context_menu.py`,
`FileItemContextMenu::show`/`WorkspacePaneWidget::showBackgroundContextMenu`, Architecture.md
§14.13):
- Right-clicking a file row or a folder row produces a menu containing the core item entries
  (Open/Rename/Delete/Copy/Cut/Properties).
- Right-clicking empty background space produces a different menu (New Folder/Paste) that does
  **not** contain the item-only entries.
- Right-clicking with a multi-selection active still produces a working menu (Copy/Cut/Delete
  present).
- Choosing "Rename" from the menu opens the same inline editor F2 does; choosing "Delete" goes
  through the same confirmation dialog the Delete key does.

Registry-sourced shell-extension entries (which vary by machine) are deliberately not asserted on —
only the small subset of entries the app itself always contributes.

### Custom Copy/Move pipeline and progress notifications
Covered (`test_copy_move_operations.py`, `FileOperationsController::pasteInto` →
`FileTransferService`, Architecture.md §14.47):
- Context-menu-triggered Copy→Paste and Cut→Paste correctly transfer a multi-file selection to a
  different folder (copy leaves the source intact; cut removes it).
- A large transfer (hundreds of small files, chosen so per-file overhead reliably stretches the
  operation to a few seconds — a single large file completes too fast to observe) shows the
  progress-notification card while in flight and the card disappears once the transfer completes,
  with every file landing at the destination.
- A completion toast containing "Copy Complete" appears after a copy finishes.
- Cancelling mid-copy via the progress card's Cancel button stops the transfer in a consistent
  state (no crash, source untouched) regardless of how far the destination got.
- Pasting onto a name collision auto-renames the incoming file (e.g. `a (2).txt`) rather than
  overwriting or prompting — the behavior `FileOperationsController::uniqueDestinationName` actually
  implements.

The new `NotificationOverlay` framework page object backing these assertions is intentionally
minimal (reading progress-card visibility, clicking Cancel, reading toast text) — see
`UiTestsGaps.md` for what a fuller Notification Center page object would still need to cover.

### Sorting
Column-header click-to-sort is covered for the Size column, confirming re-sorting actually
happens (not just that the result happens to already be sorted — sizes are parsed back from their
human-readable form, e.g. `"4.9 KB"`, to compare order across differing units), that clicking the
same header twice reverses direction, and that the active sort criterion/direction persists when the
tab navigates to a different folder without an explicit re-click (`test_details_view_sorting.py`).
The Folder Size feature's Percent column is sorted the same way (`test_folder_size.py`).

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

### Left side panel — hidden files/folders toggle
Covered (`test_hidden_files_toggle.py`, `FileListModel`'s hidden-row filter, Architecture.md §14.22),
using a real Windows `FILE_ATTRIBUTE_HIDDEN` file (via `attrib`) rather than a leading-dot name
convention:
- Flipping the View menu's "Show hidden files/folders" toggle flips whether the hidden file appears
  in the listing.
- The toggle is a genuinely app-wide `QSettings` preference, and the flipped value is still in
  effect after a full app restart, read straight from disk at startup (no re-toggle needed).

Since this setting is a real, persistent, machine-wide preference (not scoped to a test's own
`--data-dir`), every test here discovers whatever the ambient starting value already is and
restores it afterward in a `finally`, rather than assuming a known starting state.

### Left side panel — quick search toggles
The Search tab's Match Case / Whole Word / Exact Name / Match Regex / Skip Git-Ignored toggle
buttons are covered (`test_search_match_options.py`): Match Case narrowing and widening results for
a differently-cased query; the mutually-exclusive-but-optional behavior of Whole Word/Exact
Name/Regex — clicking an already-active one again reverts to plain "Contains" matching; and Skip
Git-Ignored (Architecture.md §14.45) excluding `.gitignore`d entries from results and re-including
them when toggled back off, against a real `git init`-ed folder.

### Left side panel — recursive quick search
The base recursive filename search itself (as opposed to just its toggle buttons) is covered
(`test_search_recursive.py`, Architecture.md §14.18/§14.42/§14.43/§14.46):
- Matches are found several folder levels deep, not just in the current folder.
- The list/tree view-mode toggle preserves the same match set across both renderings.
- Collapse All on the tree view correctly leaves an already-collapsed ancestor folder row collapsed
  (rather than erroring or blanking the view).
- Double-clicking a result navigates the owning tab to that result's containing folder.
- Clear empties both the result list and the query box; Refresh re-runs the same query and picks up
  files added since the last scan.
- Selecting a result row syncs the main pane's right panel to that result (e.g. the Preview tab's
  text view shows the selected file's content) — §14.43.
- Navigating the active tab away from the searched folder leaves the query box's typed text alone
  (the search itself exits), and pressing Enter afterward re-roots the search at the new folder and
  re-runs it — §14.43.
- A multi-character query typed one keystroke at a time still settles on the exact correct filtered
  set despite the 500ms input debounce (§14.46) — the debounce's keystroke-coalescing itself isn't
  directly observable via UI Automation timing, so this instead confirms the functional guarantee it
  must preserve.

### Advanced (criteria) search pane
Covered (`test_advanced_search.py`, `SearchCriteriaPanel`/`SearchResultsPane`, Architecture.md
§14.19, plus its async busy-state behavior from §14.38):
- Opening the pane (via the Search tab's "Advanced Search..." button) shows a plain, non-recursive
  listing of the current folder.
- The Name filter narrows results to matching entries.
- The Min/Max size range filters narrow results to the matching byte-size band.
- Name and size criteria combine with AND semantics.
- Closing the panel doesn't clear the Search tab's own quick-search query/results — each mode's
  state persists independently while hidden, and an explicit refresh brings the quick-search results
  straight back.
- A recursive scan over a large tree (tens of thousands of files) completes with the correct result
  and — critically — without freezing the main window, confirming the scan runs off the UI thread
  (§14.38). Catching the Search button's transient "Searching…" busy text itself proved to be an
  unreliable race on this hardware even at a ~1ms polling cadence, so that part of the assertion is
  best-effort only; the non-freezing and correct-result parts are not.

Opening the Advanced Search pane via clicking a tag chip (§14.20's tag-integrated entry point, as
opposed to the Search tab's own button) is not exercised — see `UiTestsGaps.md`.

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
