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

- **Archive browsing, preview, and extraction (§14.29)** — entirely untested; no framework support
  for navigating into an archive or triggering extraction.

- **Per-file/folder font & color customization (§14.30)** — not tested; no framework support.

- **Window-wide status bar (§14.31)** — `framework/status_bar.py` wraps the main window's own
  drive/computer-name labels, and no test reads it. (This is distinct from the *per-pane* status
  bar's selection-count/quick-select feedback, §14.28, which `test_multi_selection.py` now covers —
  see `UiTestsCoverage.md`.)

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

- **Text-preview syntax highlighting (§14.27)** — not tested. (The preview panel's plain text
  rendering itself is now incidentally exercised — see the "Partially tested" note on §14.26/§14.27
  below — but no test asserts on highlighting output specifically.)

- **Startup/exit logging and fatal exception handling (§14.23)** — not coverable by this kind of
  black-box UI test in any case (it would require crash injection), but worth noting as a feature
  with zero automated coverage of any kind.

- **Tag-integrated advanced search (§14.20)** — opening the Advanced Search pane by clicking a tag
  chip is not exercised. `test_advanced_search.py` only drives the pane via the Search tab's own
  "Advanced Search..." button (now well covered — see `UiTestsCoverage.md`); no test triggers the
  tag-chip entry point described in §14.20.

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

- **Right-panel file preview (§14.26)** — `framework/right_panel.py`'s `PreviewPanel` page object
  (`image_label`, `text_view`, `folder_list`, `loading_label`, `unsupported_label`, `error_label`)
  is now touched beyond the single `unsupported_label.exists()` check in `test_framework_demo.py`:
  `test_search_recursive.py`'s selection-sync tests select a text file via the Search tab's results
  and assert the preview's `text_view` shows its exact content. That is still the *only* path
  exercised — no test selects a result directly in the main Details view and checks the preview
  updates, and no test ever renders an image, a folder, a loading state, or an unreadable-file error
  state.

- **Custom context menu (§14.13)** — the app-owned entries (Open/Rename/Delete/Copy/Cut/Paste/New
  Folder/Properties) on a file row, folder row, and empty background space are now covered, along
  with Rename/Delete from the menu landing in the same inline-edit/confirmation paths as their
  keyboard shortcuts (`test_context_menu.py`). The *registry-sourced* shell-extension entries that
  make up the rest of a real right-click menu are still untested and deliberately so — they vary by
  machine and have no stable automation surface to assert against.

- **Address bar folder autocomplete (§14.21)** — inline ghost-completion in the address bar itself
  (what gets typed, completed, and navigated to on Enter) is now covered, including a non-ASCII
  case (`test_address_bar_autocomplete.py`). The popup's own suggestion *list* is not — and per that
  framework module's own docstring, `PaneNavigation.popup_suggestions()` currently can't see the
  popup's rows via UI Automation at all (it reports zero descendants even while genuinely visible on
  screen, confirmed by hand), because the popup is a separate non-activating top-level window rather
  than a normal child widget. This is a structural limitation to revisit, not an oversight to just
  go write a test for.

- **Notification system (§14.37)** — the progress-notification card and completion toast shown
  during a copy/move are now covered end-to-end (`test_copy_move_operations.py`, backed by the new
  `NotificationOverlay` framework page object): appearing while a transfer is in flight, disappearing
  on completion, a "Copy Complete" toast, and best-effort mid-copy cancellation. A fuller Notification
  Center capability — reading and individually dismissing several concurrent toasts, notifications
  from features other than copy/move — is explicitly out of scope for that page object (its own
  docstring calls this "Tier 3 work") and remains untested.

## Fully covered (closing former gaps — kept here for traceability)

The following were previously listed as entirely untested or partially tested and now have
dedicated coverage; see `UiTestsCoverage.md` for details. Listed here briefly so this doc's history
stays legible rather than silently dropping items:

- Keyboard hotkeys and clipboard file operations (§14.10) — `test_clipboard_operations.py`.
- Custom Copy/Move with progress notifications' core transfer behavior (§14.47) —
  `test_copy_move_operations.py` (the notification UI itself is tracked separately above, since it's
  only partially covered).
- Opening files with the default application (§14.11) — `test_open_with_default_app.py` (with an
  environment caveat noted in that file and in `UiTestsCoverage.md`).
- In-view rename/delete in the main Details view — `test_details_view_rename_delete.py`.
- Hidden files/folders visibility toggle (§14.22) — `test_hidden_files_toggle.py`, including
  persistence across a restart.
- Advanced (criteria) search pane (§14.19) and async advanced search (§14.38) —
  `test_advanced_search.py`.
- Search tab follow-ups: selection sync, Enter-to-refresh, refresh-after-navigate (§14.43) —
  `test_search_recursive.py`.
- Search tab: skip Git-ignored files/folders toggle (§14.45) — `test_search_match_options.py`.
- Search tab: debounced quick search (§14.46) — `test_search_recursive.py`.
- Multi-selection mechanics (§14.16) — `test_multi_selection.py`.
- "This PC" virtual navigation location (§14.15) — `test_this_pc.py`.
- Filename search, per-tab recursive, on its own (§14.18) — `test_search_recursive.py`.
- Live directory update for a background (non-active) tab, and the non-recursive scope boundary
  (§14.33) — `test_live_update.py`.

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

- **Favorites dialog's native file/folder pickers** — `test_manage_favorites_dialog.py`'s own
  docstring explicitly notes that the dialog's own "Add File..."/"Add Folder..." buttons are not
  exercised, because they open a native `QFileDialog` that this suite has no established pattern for
  driving. All favorites in that suite are seeded via the Favorites-menu action instead. The same
  gap would apply to any other native file/folder/color picker dialog in the app.
