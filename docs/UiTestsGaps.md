# UI Tests — Gaps in Coverage

This is the companion to `UiTestsCoverage.md`: what the `ui_tests/` black-box suite does **not**
exercise today, in plain language. Section numbers (§14.x) refer to the per-feature decision log
split across `docs/features/01-workspace-navigation-and-search.md`,
`docs/features/02-preview-decoration-and-status.md`, and
`docs/features/03-monitoring-git-and-productivity.md`.

Within each item below, "no coverage" means no test file exercises that behavior at all; "partial"
means some aspect is covered (see `UiTestsCoverage.md`) but a meaningful part of the same feature is
not.

## Entirely untested features

- **Keyboard hotkeys and clipboard file operations (§14.10)** — Cut/Copy/Paste via keyboard
  shortcuts, and the resulting file-system operations, have no coverage at all. There is no test
  file for this, and the framework has no helper for sending these shortcuts to the Details view.

- **Custom Copy/Move with progress notifications (§14.47)** — one of the most recently added
  features (see git history) has no UI-level coverage: no test drives an actual copy or move, checks
  the resulting files on disk, or checks the progress-notification UI.

- **Opening files with the default application (§14.11)** — double-click/Enter-to-open behavior is
  not exercised anywhere.

- **Custom context menu (§14.13)** — the registry-sourced right-click menu on files, folders, and
  empty background space (and how its entries differ between the three) is entirely untested. The
  only right-click interaction anywhere in the suite is the Details-view *header's* column-visibility
  menu (`toggle_column_visibility` in `framework/details_view.py`), which is a different menu
  entirely.

- **In-view rename/delete** — F2 rename is covered, but only in the *left panel's folder tree*
  (`test_left_panel_tree.py`). Renaming or deleting a file/folder from the main Details view itself
  has no coverage, and no framework helper exists for it.

- **Address bar folder autocomplete (§14.21)** — not exercised; `framework`'s `address_bar` only
  supports reading/setting the full text, not typing partial input and inspecting suggestions.

- **Hidden files/folders visibility toggle (§14.22)** — the setting that shows/hides dotfiles and
  system-hidden entries has no dedicated test, even though `test_left_panel_tree.py`'s own docstring
  notes it incidentally exercises the tree revealing hidden *ancestor* folders while the setting is
  off — the toggle itself is never flipped or asserted on.

- **Startup/exit logging and fatal exception handling (§14.23)** — not coverable by this kind of
  black-box UI test in any case (it would require crash injection), but worth noting as a feature
  with zero automated coverage of any kind.

- **Advanced (criteria) search pane (§14.19)** and **tag-integrated advanced search (§14.20)** — the
  richer multi-criteria search UI (as opposed to the quick-search bar) is not exercised. The search
  panel exposes `advanced_search_button`/`click_advanced_search()` in `framework/left_panel.py`, but
  no test ever clicks it or interacts with whatever panel it opens.

- **Async advanced search (§14.38)** — no coverage; follows from the above.

- **Search tab follow-ups: selection sync, Enter-to-refresh, refresh-after-navigate (§14.43)** — not
  tested.

- **Search tab: skip Git-ignored files/folders toggle (§14.45)** — not tested; no framework element
  exists for it either, so this would need a new `AutomationID` binding first.

- **Search tab: inline Advanced Search toggle, debounced quick search (§14.46)** — not tested beyond
  the Match Case/Whole Word/Exact Name/Regex toggles already covered.

- **Right-panel file preview (§14.26)** — `framework/right_panel.py` already defines a full
  `PreviewPanel` page object (`image_label`, `text_view`, `folder_list`, `loading_label`,
  `unsupported_label`, `error_label`), but the only place it's touched is
  `test_framework_demo.py`'s single `unsupported_label.exists()` check. No test selects an actual
  image, text file, or folder and verifies the right preview mode renders, shows a loading state, or
  shows an error for an unreadable file.

- **Text-preview syntax highlighting (§14.27)** — not tested; depends on the preview panel coverage
  above.

- **Archive browsing, preview, and extraction (§14.29)** — entirely untested; no framework support
  for navigating into an archive or triggering extraction.

- **Per-file/folder font & color customization (§14.30)** — not tested; no framework support.

- **Per-pane status bar (§14.28)** and **window-wide status bar (§14.31)** — `framework/status_bar.py`
  only wraps the main window's drive/computer-name labels, and no test reads even that. A per-pane
  status bar (selection count, total size, etc.) has no framework page object at all.

- **Git status integration in the Details view (§14.32)** — the "Git" column exists in
  `TOGGLEABLE_COLUMN_TITLES` and is incidentally shown/hidden by the column-visibility tests, but its
  actual *content* (a file's git status reflected in that column) is never asserted.

- **Git branch status and checkout in the per-pane status bar (§14.35)** — not tested; no framework
  support for reading the branch label or driving a checkout.

- **Folder monitoring (§14.34)** — `framework/right_panel.py`'s `MonitoringPanel` exposes
  `start_monitoring`, `pin`, `log_view`, and `total_label`, but the only assertion anywhere is
  `test_framework_demo.py` checking `total_label.text == "Total: 0"` right after launch. Actually
  starting a monitor, making a change under it, and checking the log view/counter update is not
  tested.

- **Notification system (§14.37)** — the in-app notification/toast mechanism (used by, among other
  things, the copy/move progress feature) has no coverage and no framework support.

- **Bottom panel integrated terminal (§14.24)** — `framework/bottom_panel.py` wires up the
  `_terminal_button` internally but deliberately exposes no page object for the terminal tab's
  content; no test opens it or interacts with it.

- **"Scan Drive" (§14.40, the rest of Folder Size)** and **the Folder Size pie chart** — both are
  explicit, documented exclusions (see `test_folder_size.py`'s and `framework/bottom_panel.py`'s own
  module docstrings): Scan Drive is exposed as a plain `Element` with no convenience method since
  it's expensive and recursive across the whole drive, and the pie chart is a hand-painted
  `QPainter` widget with no per-segment UI Automation surface to assert against. These are
  acknowledged, permanent gaps rather than oversights.

## Partially tested features

- **Multi-selection (§14.16)** — only tested as a side effect of other features: selecting two files
  additively to prove tagging/favoriting a multi-selection is a no-op
  (`test_tag_panel.py`/`test_favorites_menu.py`). There's no dedicated coverage of multi-select
  mechanics themselves (shift-click range selection, Ctrl+A select-all, selection count feedback).

- **"This PC" virtual navigation location (§14.15)** — exercised constantly as a starting/ending
  point throughout the suite (e.g. `test_navigation.py`, `test_folder_size.py`), but always
  incidentally; there's no dedicated test of "This PC"'s own behavior (e.g. what it actually lists —
  drives — or navigating into a drive from it).

- **Filename search, per-tab recursive (§14.18)** — the base `SearchPanel` page object
  (`framework/left_panel.py`) supports typing a query and reading list/tree results, and
  `test_search_match_options.py` exercises it, but only through the lens of the Match
  Case/Whole-Word/Exact-Name/Regex toggles. There's no test of plain recursive search on its own
  (e.g. results from nested subfolders, the list/tree view-mode toggle, collapse-all, double-click to
  navigate to a result, clear/refresh buttons) despite the framework already supporting all of it
  (`toggle_view_as_tree`, `click_collapse_all`, `double_click`, `click_refresh`, `click_clear`).

- **Right panel tabs generally (§14.25)** — switching between Tags/Preview/Monitoring is proven to
  work mechanically (`test_framework_demo.py`), and its *persistence* is well covered
  (`test_side_panel_persistence.py`), but Preview and Monitoring's actual in-tab behavior are the
  two gaps called out above.

- **Live directory update (§14.33)** — covered well for file create/delete/modify and subfolder
  creation in the *currently active* tab (`test_live_update.py`), but not for a background
  (non-active) tab, nor for the specific "non-recursive" scope boundary the feature is documented to
  have (a change in a subfolder of the watched folder should NOT trigger an update).

- **Favorites dialog's native file/folder pickers** — `test_manage_favorites_dialog.py`'s own
  docstring explicitly notes that the dialog's own "Add File..."/"Add Folder..." buttons are not
  exercised, because they open a native `QFileDialog` that this suite has no established pattern for
  driving. All favorites in that suite are seeded via the Favorites-menu action instead. The same
  gap would apply to any other native file/folder/color picker dialog in the app.

## Structural / process gaps

- **Not part of CI or the build.** `ui_tests/` is explicitly excluded from `build.ps1` and CTest
  (per `CLAUDE.md` and the suite's own README-equivalent instructions in `run_ui_tests.ps1`) and is
  run by hand. There is no indication of this suite running automatically anywhere (e.g. a CI
  workflow) — regressions in any of the above are only caught if a developer remembers to run it.

- **No visual/pixel-level assertions.** Everything here reads UI Automation tree state (text,
  checked state, enabled state, rectangle geometry for drag operations) — nothing verifies rendered
  appearance (colors, icons, syntax-highlighting output, the pie chart, per-file font/color
  customization) since UI Automation doesn't expose pixels. Any feature whose correctness is
  fundamentally visual (§14.27, §14.30, the Folder Size pie chart) is structurally outside what this
  suite *can* cover without a different testing approach (e.g. screenshot comparison).

- **One Windows UI Automation backend only.** The suite only targets the compiled Windows
  `Exp-LORer.exe`; per `CLAUDE.md` the project is "Windows-first (Linux later)" — there is no
  equivalent black-box coverage for a future Linux build, nor would this exact framework carry over
  unchanged.
