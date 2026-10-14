# Architecture Document — Feature Decision Log: Tagging

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.9, §14.17, §14.70, §14.88, §14.89, §14.90, §14.91 — the tag panel and selection tracking, the
standalone Tag Manager dialog, the tag panel's keyboard-driven autocomplete, the selection tags
AND/OR combine mode, the Tag Manager's file-count toggle/results list/rename-merge additions, the
Tag Manager's icon/Tags-column wiring plus window/column persistence, and the tree view's nested-tag
subtree totals.
Tag-*integrated search* (clicking a tag
to search by it, and Advanced Search's own tag box) lives in
[03-search.md](03-search.md) §14.20 instead, since that section is primarily about Advanced
Search's result-building mechanics. Section numbers are **not** contiguous with this file's
neighbors — they were assigned chronologically as each feature was built and are kept stable
since code comments cite them directly. See [Architecture.md](../Architecture.md) §14 for the
full topic index across all nine files.

---
### 14.9 Tag Panel and Selection Tracking

The shared `TagListViewModel` is owned by `WorkspaceController` and retargeted (`setActiveTab`) on
every focus/tab change, refreshing three sections: the focused tab's folder-plus-ancestor tags,
live tag search results, and the selected item's tags. Selection is per-tab state
(`TabViewModel::selectedEntries()`, a `std::vector<FileNode>` since §14.16), cleared on every
navigation. The tagging target resolves to: the single selected entry; the current folder itself
(via `IFileSystemRepository::stat`) if nothing is selected; or, for a multi-selection, every
selected entry at once (§14.88 covers how their tags are combined for display).
`SQLiteTagRepository` is the first concrete
`ITagRepository` implementation, introduced for this feature. Kept synchronous on the UI thread
(§5). Tag chips render via a `FlowLayout` (wrapping row layout) and `TagChipWidget`
(read-only/addable/removable/selectable pill styles).

### 14.17 Tag Manager Dialog

A standalone modal (`TagManagerDialog`/`TagManagerViewModel`) for global tag CRUD — rename, delete
(cascading via the schema's `ON DELETE CASCADE`, §7), add without also tagging anything — distinct
from the tag panel (§14.9), which only attaches/detaches tags to a specific selection. Built
entirely on `TagManagementUseCase`'s existing `createTag`/`updateTag`/`deleteTag`/`searchTags`; no
Domain/Application/schema change. Reuses the §14.9 `FlowLayout`/`TagChipWidget` machinery. No
automated coverage beyond the already-tested use case (§11).

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

### 14.88 Tags-on-Selection Combine Mode (AND/OR)

`TagListViewModel::refreshSelectedItemTags()` (§14.9) resolves a multi-selection's tagging target
to every selected entry at once and must collapse their individual tag lists into the single list
shown/removable in the "Tags on selection" chip row. The resolve-to-multiple-targets behavior
itself shipped with no combine-mode choice (AND/intersection only, tags common to every selected
item, so every chip shown stays valid to remove from the whole selection in one action); this adds
a user-facing toggle alongside it, keeping AND as the default so existing behavior doesn't change
underneath anyone.

* **`TagListViewModel::CombineMode`** (new public nested `enum class { And, Or }`, defaults to
  `And`) plus `setSelectedItemTagsCombineMode()` (new public slot) and
  `selectedItemTagsCombineMode()` accessor. `refreshSelectedItemTags()`'s multi-target branch now
  switches on the mode: `And` keeps the existing intersection loop; `Or` unions each target's tags
  instead, deduplicating by id (first-seen order) so the union, like the intersection, never shows
  the same tag twice. `addTagToSelection`/`removeTagFromSelection` needed no change — both already
  iterate every resolved target and tolerate per-target `AlreadyExists`/`NotFound`, so removing an
  OR-mode chip that's only present on some targets already unassigns it from whichever targets
  have it and no-ops on the rest.
* **`TagPanelWidget`** adds a `QComboBox` ("AND"/"OR") next to the "Tags on selection" label
  (`addChipSection`'s header row, extended to optionally take a trailing widget), wired straight to
  `setSelectedItemTagsCombineMode()`. Left always visible/enabled rather than hidden for a
  single-item selection — both modes trivially agree when there's only one target, so hiding it
  would just be extra state to keep in sync for no behavioral benefit.
* **Not built**: the chosen mode doesn't persist across selection changes beyond the ViewModel's
  own lifetime (no settings/DB-backed preference) and isn't retargeted per tab — one shared choice
  for the whole panel, same posture as the rest of `TagListViewModel`'s shared, retargeted state.
* **Testing**: no new GTest coverage, same carve-out as §14.9/§14.70 above.

### 14.89 Tag Manager: File-Count Toggle, Files-for-Tag Results List, Rename-Merge

Three additions to the Tag Manager dialog (§14.17), none of which change its basic CRUD-on-the-global-
tag-list posture:

* **File-count toggle**: a checkable button next to the search box calls
  `TagManagerViewModel::setShowCounts(bool)`, which fetches a `Tag::Id → int` map via a new
  `ITagRepository::tagFileCounts()`/`TagManagementUseCase::tagFileCounts()` (one grouped
  `SELECT tag_id, COUNT(*) FROM FileTags GROUP BY tag_id` query — kept synchronous like every other
  `TagManagerViewModel` operation, §5). `TagChipWidget` gained a trailing, default-`nullopt`
  `std::optional<int> count` constructor parameter rendering a small secondary label when present;
  all pre-existing call sites (tag panel, search criteria panel, media viewer search panel) are
  unaffected since they never pass a count. Counts refresh after every add/rename/delete/merge.
* **Files-for-tag results list**: selecting a chip now also calls
  `TagManagerViewModel::setSelectedTag`, which requests the tag's files asynchronously via a new
  `AdvancedSearchService::requestFilesForTag`/`AdvancedSearchWorker::fetchFilesForTag` (reusing
  `findFilesWithAllTags` + the existing `buildCandidatesFromAssociations` path→`FileNode` stat
  pipeline from §14.20, whose `root` filter became `std::optional<std::filesystem::path>` so a
  global, non-folder-scoped query can skip it). Results render in a `FileBrowserView`
  (`DisplayMode::AdvancedSearchResults`) placed in a `QSplitter` below the chip row — the same
  widget/model/delegate combination Advanced Search's results pane already uses, reused as-is. Scope
  is the exact selected tag only (no nested/child-tag inclusion, consistent with §14.9/§14.88's
  string-prefix `TagHierarchy` being a display convention, not a persisted relationship); no
  double-click/open action is wired up yet. This is the first feature needing `AdvancedSearchService`
  outside a tab's `WorkspaceController`/`TabViewModel` chain, so `MainWindow` now also takes it as a
  constructor dependency, passed through from `CompositionRoot`.
* **Rename → merge prompt**: renaming to a name that collides with another existing tag no longer just
  surfaces the DB's `UNIQUE` constraint violation as a bare error. `TagManagerDialog::onRenameClicked`
  pre-checks the trimmed new name against `TagManagerViewModel::allTags()` (new thin wrapper, so the
  check isn't limited to whatever the search box currently filters to) for a case-insensitive exact
  match; if found, a confirmation dialog offers to merge instead, calling a new
  `TagManagerViewModel::mergeTags`/`TagManagementUseCase::mergeTags`/`ITagRepository::mergeTags`. The
  repository implementation is a transaction — `INSERT OR IGNORE` the source tag's `FileTags` rows
  under the target tag id (the composite `PRIMARY KEY (file_id, tag_id)` would otherwise reject rows
  for files already carrying both tags), delete the rest, then delete the source `Tags` row. The
  pre-existing DB-level `UNIQUE` violation path remains as a safety net for the rare race between the
  check and the update. Since tag "children" are a `TagHierarchy` string-prefix convention with no DB
  edge, merging a tag away never touches any other tag's row.
* **Testing**: `tagFileCounts`/`mergeTags` get real coverage — `tests/adapters` (in-memory SQLite, per
  §11) for the repository methods, `tests/application` (mocked `ITagRepository`) for the use-case
  passthrough/guards. UI pieces keep the same no-GTest carve-out as the rest of this dialog.

### 14.90 Tag Manager: Real Icons/Tags Column for Results List, Window/Column Persistence

The §14.89 results list never got real per-row icons or Tags-column data — `TagManagerDialog::
onFilesForTagChanged` only ever called `FileListModel::setEntries()`, never `setIcons()`/
`setFileTags()`, so every row showed the generic placeholder icon and a permanently blank Tags
cell. This is the same bug *class* §14.38/the "[search] Fix bug: not correct icons for files were
shown" fix addressed for Advanced Search — a `FileListModel` row only lights up once something
calls those two setters — just never wired up here at all, rather than wiped by a re-filter.

* **Icons/Tags wiring**: `TagManagerViewModel` gained two new constructor dependencies —
  `IconLoadingService&`/`FileTagsService&`, both already owned long-lived by `CompositionRoot`
  and already used by `TabViewModel::requestAdvancedSearchResultIcons`/
  `requestAdvancedSearchResultTags` for Advanced Search's own results. `setSelectedTag` now
  requests both, in addition to the existing `AdvancedSearchService::requestFilesForTag` call,
  emitting two new signals (`filesForTagIconsChanged`/`filesForTagTagsChanged`) that
  `TagManagerDialog` applies straight onto its results model via `setIcons()`/`setFileTags()`.
  Unlike Advanced Search, no snapshot-accumulator cache was needed — this results list never
  re-filters while open (`setEntries()` runs exactly once per chip click), so there's no
  icon-cache-wipe to guard against. A new `int` generation counter guards all three async
  replies (the pre-existing `requestFilesForTag` callback included, which had no staleness guard
  before this) against a rapid chip-switch resolving out of order — the same discard-by-
  generation idiom `TabViewModel` already uses throughout. `MainWindow`/`main.cpp` now thread
  `IconLoadingService&`/`FileTagsService&` through to `TagManagerViewModel`'s construction, the
  same way `AdvancedSearchService&` was threaded through for §14.89.
* **Window/column persistence**: the dialog's size (previously a hardcoded 800×600 every open)
  and the results list's column widths, left-to-right order, and per-column visibility are now
  saved/restored via `QSettings` (`"TagManager/WindowGeometry"`/`"TagManager/ColumnWidths"`/
  `"TagManager/ColumnOrder"`/`"TagManager/ColumnVisibility"`), read in the constructor — via the
  existing `FileBrowserView::setColumnWidths`/`setColumnOrder`/`setColumnVisible`, the same trio
  the header's right-click column menu already drives — and written from a `done(int)` override
  (not `closeEvent`, since the dialog's own "Close" button calls `QDialog::accept()` directly
  rather than closing the window) — a self-contained choice over extending `AppConfig`'s JSON,
  since this dialog is a fresh, stack-local `QDialog` with no existing `CompositionRoot`/
  `AppConfig` plumbing of its own.
* **Count label**: the per-chip file-count badge (§14.89) is now bold, to read as a count rather
  than a continuation of the tag name next to it.
* **Testing**: no new GTest coverage, same carve-out as §14.17/§14.89's UI pieces.

### 14.91 Tag Manager Tree/List: Nested-Tag Subtree Totals

Both of the Tag Manager's view modes' per-tag count (§14.89's toggle) previously showed only a
tag's own file count, with no indication of how many files sat under its nested children (still
the §14.9/§14.88/§14.89 string-prefix `TagHierarchy` display convention, never a DB relation).

* **Tree view**: `TagManagerDialog::rebuildTagTree` now does a post-order pass over the
  already-built tree (after counts are known) computing each node's subtree total — its own count
  (if it's a real tag, not just a path-grouping node like "Project" when only "Project/Foo" is an
  actual tag) plus the recursive sum of every descendant tag's own count. A leaf real tag keeps the
  pre-existing `"Name  (N)"` format; a real tag with children shows both, `"Name  (N, M total)"`;
  a path-grouping node (always has children, by construction) shows only the total, `"Name  (M
  total)"`.
* **List view (chips)**: the flat chip list has no tree structure to walk, so
  `TagManagerDialog::rebuildTagChips` recomputes "children" the same way — for each tag, every
  *other* currently-matching tag whose name is prefixed `"<tag name>/"` — and sums their counts.
  `TagChipWidget` gained a fourth constructor parameter, `std::optional<int> totalCount` (ahead of
  the pre-existing trailing `parent`; no other call site passes it), rendering `"count/totalCount"`
  in the count label instead of a bare count when a tag has children; a childless tag's chip is
  unchanged.

Purely a `TagManagerDialog`/`TagChipWidget` rendering change over data
`TagManagerViewModel::countForTag` already provides — no new Port/use-case/repository method. Same
no-GTest carve-out as the rest of this dialog.
