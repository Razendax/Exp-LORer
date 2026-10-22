# Architecture Document — Feature Decision Log: Decoration

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.30 — per-file/folder font & color decoration. Split out of
[features/05-preview-and-decoration.md](05-preview-and-decoration.md) once that file approached
Architecture.md §14's 1000-line-per-file cap (its own intro explains why); this topic was always
distinct from that file's actual subject (the right-panel Preview tab) and only shared a file with
it by historical accident of the original split being done in one pass. Section numbers are
**not** contiguous with this file's neighbors — they were assigned chronologically as each feature
was built and are kept stable since code comments cite them directly. See
[Architecture.md](../Architecture.md) §14 for the full topic index across all files.

---
### 14.30 Per-File/Folder Font & Color Customization (Pattern-Based)

Lets the user define an ordered list of **decoration rules** — each a comma-separated set of
wildcard name patterns plus a style (color, font family override, bold/italic/underline/strikeout)
— edited from a new `Settings → General → Colors` page. Every row in every file listing (normal
browsing, quick search §14.18, advanced search §14.19 — all seven view modes) is checked against the
rule list; the first rule whose pattern list matches wins and its style is applied to the whole row.
Like syntax highlighting (§14.27) and the terminal (§14.24), this is a pure presentation concern:
**no Domain/Application change, no new Port, no `CompositionRoot` use-case wiring** — only new
adapter-layer pure logic plus Qt-facing config/ViewModel/UI, mirroring the
`HighlightTheme`/`HighlightThemeStore`/`HighlightThemeViewModel` stack.

* **Pattern syntax:** `*` (any run of characters), `?` (exactly one character), `"..."` (a quoted
  span is taken literally, so a comma inside it doesn't split the pattern list; a doubled `""`
  inside one is a literal `"`), and a leading `#` on an individual pattern (stripped before
  matching) which marks that pattern as applying to **folders** instead of the default **files
  only**. Multiple patterns for one rule are comma-separated with OR semantics (any one matching is
  enough). Matching is against `FileNode::name()` (the last path component, not the full path),
  case-insensitive on ASCII only (non-ASCII case-folding is out of scope). Worked examples: `*.exe`
  (files only), `Image_?.png` matches `Image_1.png`/`Image_2.png` but not `Image_2121.png`, `#.git`
  (a folder named exactly `.git`), `"Image, file.png"` (one pattern containing a literal comma).
* **`src/adapters/decoration`** (new module, `explorer_adapters_decoration`, pure C++ — no Qt, same
  posture as `adapters/highlighting`'s `LanguageRegistry`): `FileNamePattern` (parses/strips the `#`
  marker, glob-matches `*`/`?` against a UTF-8 name decoded to codepoints so `?` matches one
  *character*, not one UTF-8 byte); `FileDecorationRule` (a pattern list plus style fields —
  `std::optional<std::string> hexColor`/`fontFamily`, `std::optional<int> fontPointSize`,
  `bool bold/italic/underline/strikeout` — `create()` returns a `Result` so an unterminated `"` is a
  reportable parse error, not a crash);
  `FileDecorationRules` (an ordered `std::vector<FileDecorationRule>`,
  `resolve(nameUtf8, isDirectory)` returns the first match or `nullptr` — **first-match-wins, not
  merged**, so rule order is priority and the Settings page exposes Up/Down reordering). GTest-
  covered like `LanguageRegistry`/`SyntaxHighlightEngine`.
* **Built-in Hidden Files/Hidden Folders rows (§14.22):** `FileDecorationRules` additionally holds
  two always-present `FileDecorationRule` members — `hiddenFilesRule()`/`hiddenFoldersRule()`
  (read) and `setHiddenFilesRule()`/`setHiddenFoldersRule()` (write), selected by
  `hiddenStyle(isDirectory)` — that are **structurally excluded from `resolve()`**: they're stored
  outside `m_rules` and never participate in name-pattern matching or Up/Down priority. They start
  with no color/font override (both fields unset, same "(Default)" starting point `Add Rule`
  produces) until the user sets one — there is deliberately no hardcoded hidden-dim fallback
  anymore. `FileDecorationRulesStore` persists them as two optional top-level JSON keys,
  `hiddenFiles`/`hiddenFolders`, holding the same `color`/`fontFamily`/`fontSize`/`bold`/`italic`/
  `underline`/`strikeout` fields as a rule object but no `patterns` key; missing or malformed keys
  (old-format file, hand-edited JSON) fall back to the same no-override default, matching the
  store's tolerant-load posture. `FileDecorationsViewModel` exposes matching
  `hiddenFilesRule()`/`hiddenFoldersRule()`/`setHiddenFilesRule()`/`setHiddenFoldersRule()`, each
  broadcasting `rulesChanged()` and queuing a save exactly like `setRules()`.
* **`src/adapters/config`** gains `FileDecorationRulesStore` (own `file_decorations.json`, same
  tolerant-load/atomic-save shape as `HighlightThemeStore`), an `AsyncSaveDispatcher<FileDecorationRules>`
  member (the shared background-thread-save helper every `*ViewModel` in this directory uses — see
  the `AsyncSaveDispatcher`/`AsyncSaveWorker` note below), and `FileDecorationsViewModel` (the
  live `QObject` wrapper — `rules()`/`setRules()`, `rulesChanged()` — owned once by
  `CompositionRoot`/`MainWindow` like `HighlightThemeViewModel` so
  `SettingsDialog` and every `FileListModel` observe the same live rule set).
* **Threading into every `FileListModel` — deliberately *not* a `FileDecorationsViewModel&`:**
  `src/adapters/config` already depends on `src/adapters/viewmodels` (`AppConfig` needs
  `FileListModel::ColumnCount`), so a `FileDecorationsViewModel&` constructor parameter on
  `FileListModel`/`TabViewModel`/`WorkspacePaneViewModel`/`WorkspaceController` (all in
  `src/adapters/viewmodels`) would create a build-graph cycle. Instead, the same
  `CompositionRoot::createWorkspaceController()`/`createTabViewModel()` →
  `WorkspaceController` → `WorkspacePaneViewModel` → `TabViewModel` → each of its three
  `FileListModel` instances (main/quick-search/advanced-search) chain §14.20 used for
  `TagManagementUseCase&` threads **two** plain parameters instead: `const FileDecorationRules&`
  (from `src/adapters/decoration`, which has no such dependency, for the actual name matching) and
  a bare `QObject&` — the same `FileDecorationsViewModel` instance, referenced only as its `QObject`
  base. `FileListModel` connects to that `QObject&`'s `rulesChanged()` signal via the string-based
  `SIGNAL`/`SLOT` overload of `connect()`, which needs no compile-time knowledge of
  `FileDecorationsViewModel` and so pulls in no dependency on `src/adapters/config`. On that signal,
  it re-emits `dataChanged()` across `Qt::ForegroundRole`/`Qt::FontRole` for every row, so
  already-open listings and search results restyle live the moment a rule is added/edited/removed/
  reordered — the same immediate-apply posture every other Settings page uses.
  `explorer_adapters_viewmodels` links `explorer_adapters_decoration` directly for this.
* **`FileListModel::data()`:** resolves a decoration via a single `resolveDecoration(entry)` point
  that first tries `m_fileDecorationRules.resolve(PathUtf8::toUtf8(entry.name()),
  entry.isDirectory())` — **never `path::string()`**, per §6 — skipped for synthetic rows
  (`entry.displayName()` set, e.g. "This PC"'s drives, §14.15), since they have no real on-disk
  filename to match against. If no pattern rule matches and `entry.isHidden()`, it falls back to
  `m_fileDecorationRules.hiddenStyle(entry.isDirectory())` — so a matching name-pattern rule always
  wins over the built-in hidden style for a hidden entry, and an entry that is neither matched nor
  hidden resolves to nothing. A resolved `hexColor` becomes the `Qt::ForegroundRole` (unset when
  nothing resolves or the resolved rule has no color override); a resolved font override/style bits
  produce a new `Qt::FontRole` value (unset otherwise). Neither role is column-scoped — a match
  styles the whole row across every Details-view column, not just Name.
* **Delegates:** `FileIconDelegate`/`FileTileDelegate`/`SearchResultDelegate` already read
  `Qt::ForegroundRole` off the model for their hand-painted text pen color (§14.22); each gains the
  symmetric `Qt::FontRole` read to `painter->setFont(...)`, with `SearchResultDelegate` layering its
  existing match-highlight bold *on top of* the resolved font rather than replacing it. List/Details
  view modes need no change — Qt's stock delegate already honors both roles.
* **UI:** `SettingsDialog` gains a `General → Colors` tree item (sibling of the existing
  `General → UI` item) and a `FileDecorationsViewModel&` constructor parameter (same posture as its
  existing `HighlightThemeViewModel&`), threaded from `MainWindow` the same way. Its page is a
  `QTableWidget` (Patterns / live style Sample / ↑ / ↓ / Remove, one row per rule) plus "Add Rule".
  Patterns/Sample columns are `Interactive`-resized (user-draggable, with sane default widths) and
  the ↑/↓/Remove columns are `ResizeToContents`-sized `QToolButton`s with standard icons
  (`SP_ArrowUp`/`SP_ArrowDown`/`SP_TrashIcon`) rather than text buttons, so the action columns stay
  compact instead of dominating the row. Reordering and removal call `setRules()` immediately (no
  confirmation — cheap, reversible). The table's first two rows are always the built-in "Hidden
  Files"/"Hidden Folders" rows (a fixed label in the Patterns column instead of pattern text,
  editing their live style Sample the same way as any rule) — they have no ↑/↓/Remove buttons (not
  reorderable relative to pattern rules, not removable) and sit above the pattern-rule rows, which
  render starting at table row 2 keyed by their vector index for Up/Down/Remove. A new
  `FileDecorationRuleDialog` (`src/ui/widgets`) edits one rule: a pattern `QLineEdit` (with a syntax
  hint covering the four worked examples above) — disabled and showing a fixed placeholder instead
  (with the syntax hint hidden) when editing one of the two built-in hidden rows, whose `Kind`
  constructor parameter (`PatternRule`/`HiddenFiles`/`HiddenFolders`) selects this mode and skips
  pattern parsing on accept (`FileDecorationRule::create("", ...)`, always valid); a color
  row whose button *is* the swatch (background = the chosen color, text = its hex code or
  "(Default)", click opens `QColorDialog`) with a "Reset" button to unset it; and a "Font" group box
  replacing the old `QFontDialog` picker (whose Bold/Italic/Underline/Strikeout never actually
  applied — only `family()` was read from it, which confused users seeing effects in that dialog
  that had no effect). The group box's left column stacks an editable `QLineEdit` (a quick-selection
  filter: typing jumps/highlights the first matching row in the list below without altering what was
  typed) above a `QListWidget` of every `QFontDatabase::families()` entry, with no "(Default)" row —
  picking a list row fills the edit field, and the edit field's own (possibly empty) text is the
  single source of truth read on OK, empty meaning "no family override". To its right, a `QListWidget`
  of `QFontDatabase::standardSizes()` (plus the rule's existing size if it's non-standard) serves as
  the size selector, again with no "(Default)" row — no selection means "no size override". Below the
  family column sit the independent Bold/Italic/Underline/Strikeout checkboxes (the single source of
  truth for style bits — no competing dialog-level style control), with a live preview label to their
  right (next to the size list) showing the combined family/size/effects/color. OK validates via
  `FileDecorationRule::create()` and shows an inline error on an unterminated quote rather than
  closing.
* **Not built:** per-folder/per-tab rule scoping (rules are global app-wide, matching every other
  Settings preference in this app); merging multiple matching rules' styles; theme import/export or
  preset packs; a live-regex pattern language beyond the specified `*`/`?`/`"`/`#` mini-glob;
  non-ASCII case folding; decorating synthetic entries ("This PC", drives, quick-access folders).
  No automated coverage for the UI pieces (`SettingsDialog`'s new page, `FileDecorationRuleDialog`,
  `FileListModel`'s new role branches, the delegates' `FontRole` handling) beyond the existing §11
  UI/ViewModel-glue carve-out; the pure `FileNamePattern`/`FileDecorationRule`/`FileDecorationRules`
  logic and `FileDecorationRulesStore`'s round trip are GTest-covered.
