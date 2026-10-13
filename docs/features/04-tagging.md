# Architecture Document — Feature Decision Log: Tagging

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.9, §14.17, §14.70, §14.88 — the tag panel and selection tracking, the standalone Tag
Manager dialog, the tag panel's keyboard-driven autocomplete, and the selection tags AND/OR
combine mode. Tag-*integrated search* (clicking a tag
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
