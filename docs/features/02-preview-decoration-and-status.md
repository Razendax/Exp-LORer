# Architecture Document — Feature Decision Log: Preview, Decoration & Status Bars

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.27–§14.33. Preceded by
[01-workspace-navigation-and-search.md](01-workspace-navigation-and-search.md) (§14.1–§14.26);
continued in
[03-monitoring-git-and-productivity.md](03-monitoring-git-and-productivity.md) (§14.34–§14.43),
[04-search-persistence-and-file-tools.md](04-search-persistence-and-file-tools.md) (§14.44–§14.56),
and [05-media-viewer-and-workspaces.md](05-media-viewer-and-workspaces.md) (§14.57–§14.70).

---

### 14.27 Text-Preview Syntax Highlighting

Recognized text/source files shown in the Preview tab (§14.26) are syntax-highlighted using
tree-sitter as the parsing engine, plus a `File > Settings...` dialog letting the user recolor each
language's tokens. Like the integrated terminal (§14.24), tree-sitter is a plain C library — not
Qt/SQLite/FFmpeg — but this is a presentation concern, not a Domain/Application business rule, so
**no Application-layer Port and no `CompositionRoot` use-case wiring** are introduced.

* **Grammar sourcing.** vcpkg's public registry ships only tree-sitter's core C library plus a
  `tree-sitter-c` grammar — no C++/C#/Python/JS/TS/JSON/HTML/CSS grammars. Those are sourced via
  small **vcpkg overlay ports** (§6) — `vcpkg-overlays/ports/tree-sitter-<lang>/`, one per grammar,
  each fetching that grammar's upstream release archive (`vcpkg_from_github`, pinned tag) and
  compiling its `src/parser.c` (+ `src/scanner.c`/`.cc` where present, e.g. C++/TypeScript/Python)
  into a static lib, installing the header plus that grammar's own `queries/highlights.scm`.
* **Language scope (this pass):** C, C++, C#, Python, JavaScript, TypeScript, JSON, HTML, CSS. The
  remaining text extensions §14.26 already accepts (txt/xml/md/ini/log/yaml/csv/sql/sh/bat/ps1/
  xaml/toml) keep rendering as plain, unhighlighted text — a documented scope exclusion, not a
  limitation of the mechanism (more languages are added by vendoring more overlay-port grammars).
* **`src/adapters/highlighting`** (new, pure logic, no Qt except one resource-loading function):
  `Language` (plain enum, one per supported language); `LanguageRegistry::languageForExtension`
  (extension → optional `Language`, same static-dispatch shape as `MediaExtensions`);
  `SyntaxHighlightEngine` — owns one `TSParser`/compiled `TSQuery` per `Language` (lazy-built,
  query text loaded from that language's bundled `highlights.scm` via Qt resources, the same
  mechanism `resources/qml.qrc` already uses). `highlight(Language, source)` returns
  `{byteOffset, byteLength, captureName}` spans; `captureNamesFor(Language)` returns exactly the
  capture names that language's own `highlights.scm` defines (e.g. CSS has no `function` capture),
  so the Settings dialog never shows a hardcoded universal token list. GTest-covered like
  `AnsiTerminalBuffer` (`tests/adapters/SyntaxHighlightEngineTest.cpp`) — no Qt/disk/SQLite touched.
* **`src/adapters/config`** gains a second store, deliberately separate from `AppConfig`/
  `AppConfigStore` because it is a persistent user *preference* (edited from Settings, saved
  immediately) rather than session/window state (saved only on close, §14.14): `HighlightTheme` — a
  language → capture-name → hex-color map, seeded with built-in defaults (one shared base palette:
  keyword/blue, string/green, comment/gray, number/teal, function/purple, type/cyan,
  variable/operator/default-foreground) wherever the user hasn't overridden a token —  and
  `HighlightThemeStore`, the same tolerant-load/atomic-save shape as `AppConfigStore`, its own
  `highlight_theme.json` alongside `config.json`/`explorer.db` under the app-data directory.
* **`src/ui/widgets`:** `SyntaxHighlighter` (`QSyntaxHighlighter` subclass) attached to
  `PreviewPanelWidget`'s text view — `showText()` sets the active `Language` from the previewed
  path's extension (unmapped extensions stay unhighlighted, as today); since preview text is capped
  and read-only, spans are computed once per preview load, not per keystroke, and a `themeChanged()`
  signal triggers `rehighlight()` so an open preview updates live from Settings. `SettingsDialog`
  (new) — a `QTreeWidget` category tree on the left (Highlight → Languages → one entry per
  `Language`) and a `QStackedWidget` on the right listing the selected language's capture names
  (from `captureNamesFor`) each with a `QColorDialog`-backed color swatch; a single "Close" button,
  no OK/Cancel, edits apply and persist immediately (same posture as `TagManagerDialog`'s tag
  edits) — structured generically so a future non-highlighting settings category slots in without
  reshaping the dialog. `HighlightThemeViewModel` (new, `adapters/viewmodels`) wraps
  `HighlightTheme`/`HighlightThemeStore` (`tokenColor`/`setTokenColor`/`themeChanged`), owned once
  by `CompositionRoot`/`MainWindow` like `TagListViewModel` so `SettingsDialog` and every
  `SyntaxHighlighter` observe the same live theme. `MainWindow::createMenuBar()` adds
  `File > Settings...`, opening `SettingsDialog`. `SettingsDialog` also hosts a second, non-
  highlighting category — General > UI, one `QCheckBox` ("Show close button on tabs") — exercising
  the "future category slots in without reshaping the dialog" extension point above: its own
  `QSettings`-backed key (`WorkspacePaneWidget::kShowTabCloseButtonsSettingsKey`,
  `"UI/ShowTabCloseButtons"`, default on) and a `showTabCloseButtonsChanged(bool)` signal that
  `MainWindow` fans out to every pane's `WorkspacePaneWidget::setTabCloseButtonsVisible()`, same
  propagation shape as the existing "Show hidden files/folders" View-menu toggle (§14.22).
  Separately, `src/ui/widgets/UiColors.h` (new, header-only) centralizes the fixed, muted-blue hex
  constants used for panel/tab visual-distinction styling (active pane/tab tint, bottom/right panel
  header tint, advanced-search button, tags-panel section backgrounds) — plain `setStyleSheet` calls
  scoped by `objectName`, not a theming engine; the app has no dark-mode/theme-switching support.
* **Not built:** highlighting for the plain-text extensions listed above; highlighting anywhere
  outside the Preview panel (e.g. not inside the §14.24 terminal); theme import/export, light/dark
  presets, or a "reset to defaults" button; automated UI coverage for `SettingsDialog`/
  `SyntaxHighlighter` beyond `SyntaxHighlightEngine`'s own GTest coverage (§11's existing UI/
  ViewModel-glue carve-out).

### 14.28 Per-Pane Status Bar

A persistent status bar shown at the bottom of every pane, reporting the currently focused
tab's object count / selection count and hosting a quick-select field — distinct from
`MainWindow`'s existing `QMainWindow::statusBar()` (a transient, window-wide 5-second
auto-hiding message bar for navigation/search failures, `MainWindow::onStatusMessage`), which is
unchanged by this feature.

* **One widget per pane, rebound to the active tab** — the same shape `WorkspacePaneWidget`
  already uses for its toolbar (address bar, back/forward/up): a single `StatusBarWidget`
  instance, not one per tab page, so the existing per-tab splitter/`QStackedWidget` structure
  (`addPageForTab`, §14.24) is untouched. `bindToolBarToTab()` — already the tab-switch rebind
  point — additionally wires the new tab's `directoryContentsChanged`, `searchResultsChanged`,
  `advancedSearchResultsChanged`, `searchModeChanged`, `advancedSearchModeChanged`, and
  `selectedEntriesChanged` signals to a `refreshStatusCounts()` handler that reads the counts
  directly off the tab (`advancedSearchActive()`/`searchActive()` pick which of
  `fileListModel()`/`searchResultsModel()`/`advancedSearchResultsModel()`'s `rowCount()` is
  "current", plus `selectedEntries().size()`), so it's correct regardless of which signal fired.
* **`StatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`: a `QLabel` (`"<N> items"`, or `"<N> items, <M> selected"` once something is
  selected) plus a `QLineEdit` quick-select field emitting `quickSelectTextChanged(QString)` on
  every keystroke; Escape clears it. No `TabViewModel`/`FileListModel` dependency — it only
  emits signals, `WorkspacePaneWidget` mediates, exactly like the address bar's
  `folderSuggestionsRequested` (§14.21). Fixed height, appended below `m_tabWidget` in
  `WorkspacePaneWidget`'s own top-level layout (not inside any per-tab splitter).
* **Quick select** (`FileBrowserView::selectEntriesContaining(text)`, new) — a linear scan over
  the currently active tab page's model (mirroring `indexForPath`'s existing scan), matching
  against the Name column's *displayed* text (`Qt::DisplayRole`, so synthetic `displayName()`
  overrides like "This PC" drives, §14.15, match what's typed) rather than `FileNode::name()`
  directly. Every case-insensitive substring match (anywhere in the name, not just a prefix) is
  selected; the first match is scrolled into view. Empty text is a no-op (leaves the existing
  selection alone); no match clears the selection. `WorkspacePaneWidget` resolves "the currently
  active tab page's browser view" via a new `currentBrowserView()` helper, factored out of the
  unwrap `onViewModeChanged()`/`currentColumnWidths()` already duplicated (splitter →
  `QStackedWidget` → `FileBrowserView`-or-`SearchResultsPane::browserView()`).
* **Reset points:** the quick-select field is cleared (`StatusBarWidget::clearQuickSelect()`) on
  tab switch and on the bound tab's `currentPathChanged`/search-mode-toggle signals, since the
  visible item set changes completely at each of those points — same ephemeral-per-context
  posture as the quick-search/advanced-search boxes' own state (§14.18/§14.19).
* **Not built:** cycling through multiple matches (Tab/F3) — all matches are selected at once;
  persisting the quick-select text across navigation or restart; per-tab status bar instances.
  No Domain/Application change — every count and listing the bar needs already exists on
  `TabViewModel`/`FileListModel`. No automated coverage: pure UI/ViewModel glue, same carve-out
  as the toolbar and address-bar autocomplete (§11).

### 14.29 Archive Browsing, Preview, and Extraction (Read-Only)

Lets the user browse into a `.zip`/`.7z`/`.tar`/`.tar.gz`/`.tar.bz2` archive (or a **read-only**
`.rar`) as if it were a folder, preview its contents in the right panel, and extract from it — no
archive creation or in-archive editing in v1.

* **Archives as virtual paths.** Exactly the same posture as "This PC" (§14.15): a real archive
  file's own path, once entered, *is* the navigation location for everything below it — e.g.
  `C:\Downloads\photos.zip\vacation\img.jpg` is a legal `std::filesystem::path` (no real file backs
  it) that `parent_path()`/`filename()`/`/`-append all handle correctly with zero special-casing
  outside `StandardFileSystemRepository`. Every existing path-based mechanism (`TabViewModel::
  navigateTo`, history, breadcrumbs, session persistence, §14.21 address-bar autocomplete, sort/
  view-mode/multi-selection/status bar) therefore works unmodified. `Up`/`Backspace` from an
  archive's root lands on its real containing folder for free, via the same `parent_path()` call
  every other navigation already uses.
* **One new Port method**, `IFileSystemRepository::extractArchive(archiveFile,
  destinationDirectory)` (extracts everything under `archiveFile`, or under a folder-inside-an-
  archive path, preserving relative structure) — every other interaction reuses existing Port
  methods, made archive-aware only inside `StandardFileSystemRepository`:
  * `listDirectory`/`stat` on an archive-rooted path return synthesized `FileNode`s (directories
    inferred from entry-path prefixes when a format doesn't carry explicit directory entries, e.g.
    zip; size 0 and the archive's own `modificationDate` for those synthesized directories).
  * `copy(source, destination)` extracts instead of `fs::copy` only when `source` resolves *inside*
    an archive; a real, not-yet-entered archive file as `source` is unaffected (still a literal
    byte copy — Ctrl+C/Ctrl+V on a `.zip` still duplicates the `.zip`). This is what makes
    Copy→Paste out of an archive into a real folder work with no new UI plumbing, and it shares one
    internal extraction helper with `extractArchive` itself.
  * `move`/`moveToTrash`/`deletePermanently`/`createDirectory`/`createFileFromTemplate` return
    `Error(ErrorCode::InvalidArgument, "Cannot modify a read-only archive")` when the target is
    inside an archive — defense-in-depth; the UI also disables these proactively.
  * `openWithDefaultApplication` on a file entry inside an archive extracts it to a per-session
    temp directory (`%TEMP%\Exp-LORer\archive-preview\...`, not cleaned up by the app — relies on
    normal OS temp lifecycle) and launches the extracted copy — mirrors Explorer's/7-Zip's own
    zip-folder "open" behavior.
  * `listDirectoryRecursive` rejects an archive-rooted `root`, the same way it already rejects
    `VirtualPaths::ThisPC` — recursive search does not reach inside archives in v1.
* **`FilePreviewUseCase`** (§14.26): the folder-preview branch's condition widens from
  `target.isDirectory()` to `target.isDirectory() || ArchiveExtensions::isArchiveExtension(...)` —
  an archive file gets the identical `FilePreview::folder(entries)` treatment a real folder does,
  since `listDirectory()` on its own path already returns its top-level entries once the adapter
  change above lands. `PreviewPanelWidget` needs no changes — it already renders `Folder` kind.
* **`src/application/ArchiveExtensions.h`** (new, pure, mirrors `MediaExtensions.h`): the
  supported-extension list, plus a pure/lexical (no I/O) `archiveAncestorInPath(path)` used only by
  the UI layer as a cheap hint for which context-menu actions to show — the authoritative,
  disk-verified check lives in the adapter (below). A UI/adapter mismatch (e.g. a real folder
  literally named `foo.zip\`) degrades to a disabled button or a graceful `Result` failure, never a
  crash.
* **`src/adapters/archive`** (new module, `explorer_adapters_archive`, mirrors `adapters/media`):
  `ArchivePathResolver` (walks a path's ancestors for the outermost real, on-disk archive file —
  nested archives are not resolved further; navigating into an archive-looking entry inside another
  archive fails gracefully as `NotFound`, not a crash — explicit v1 exclusion); `LibArchiveReader`
  (thin wrapper over libarchive's `archive_read_*`, only the in-scope formats/filters enabled, not
  `archive_read_support_format_all()`); `ArchiveIndexCache` (small in-memory-only LRU, ~4 archives,
  keyed by `(path, size, mtime)` — same staleness-free posture as `ThumbnailCache`, §8 — so
  navigating between folders inside one archive doesn't re-scan the whole stream each time).
* **UI wiring**: `WorkspacePaneWidget::wireBrowserView`'s `itemActivated` handler navigates
  (instead of calling `openFile`) whenever the target is a directory *or* an archive-extensioned
  entry — satisfying "opening an archive doesn't launch an external app." `ContextMenuBuilder::
  NativeActions` gains two nullable actions, `extractHere`/`extractTo`, shown when every selected
  path is a real archive file; while the tab is browsing inside an archive, `cut`/`copy`/`paste`/
  `rename`/`deleteAction`/`newFolder`/`properties` are disabled (OS-clipboard copy needs a real
  `CF_HDROP` path, which a virtual archive entry isn't) and the same Extract actions target the
  in-archive selection instead. The bottom-panel terminal's cwd fallback (§14.24, already guarding
  `cwd == VirtualPaths::ThisPC`) gains the same guard for an archive-rooted `cwd`.
* **Not built:** archive creation/add-to-archive/any write support; nested-archive browsing;
  recursive/advanced search reaching inside archives; tagging a target inside an archive (disabled
  — `computeFileHash`'s rename/move-relocation fallback can't be computed for a virtual entry
  without a full extraction); guaranteed type-specific icons for in-archive entries
  (`QFileIconProvider` needs a real on-disk file; may fall back to a generic icon); RAR5 coverage
  beyond whatever the vcpkg-pinned libarchive version supports (legacy RAR4 is the safe floor).
  Adapter-layer pieces (`ArchiveExtensions`, `ArchivePathResolver`, `LibArchiveReader`, the
  archive-aware `StandardFileSystemRepository`/`FilePreviewUseCase` paths) are GTest-covered per
  §11; the UI-layer changes above fall under the existing UI/ViewModel-glue carve-out, smoke-tested
  manually.

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

### 14.31 Window-Wide Status Bar

A persistent, window-wide bar at the very bottom of the application — outside any pane/tab, unlike
`StatusBarWidget` (§14.28, one instance per pane, item/selection counts + quick-select). Shows,
left-aligned, the focused pane's active tab's drive (`"<label> — <free> free of <total>"`) and,
centered, the machine's computer name (`QSysInfo::machineHostName()`, set once — no Port, not
filesystem-related).

* **Reuses `QMainWindow::statusBar()`** rather than a second independent bottom widget — it is
  already bottom-of-window, full-width, and tab-agnostic. `statusBar()` was previously used only
  for `MainWindow::onStatusMessage`'s transient, 5-second auto-hiding failure messages
  (`showMessage()`), which is unchanged. `QStatusBar` keeps "normal" widgets (`addWidget`, hidden
  while a transient message shows) separate from "permanent" widgets (`addPermanentWidget`, never
  hidden) — the new content is added as a single permanent widget with stretch factor 1
  (`statusBar()->addPermanentWidget(m_mainStatusBarWidget, 1)`), so with nothing else competing for
  space it expands to the whole bar width in the common case, only compressing toward the right
  during the rare seconds a transient message is showing.
* **`MainStatusBarWidget`** (new, `src/ui/widgets`) — a plain display widget, same posture as
  `AddressBarWidget`/`StatusBarWidget`: an internal `[driveLabel, stretch, computerNameLabel,
  stretch]` `QHBoxLayout` (the second stretch both centers the computer name and reserves room for
  a possible future right-hand section) and one setter, `setDriveInfoText(QString)` — empty string
  blanks it. No `TabViewModel`/use-case dependency.
* **`IFileSystemRepository::driveSpaceInfo(path)`** (new Port method) resolves the drive/volume
  containing `path` (any path on it) to a `DriveSpaceInfo{ displayLabel, freeBytes, totalBytes }`
  (new plain-data type, `src/application/DriveSpaceInfo.h`) — `displayLabel` is fully resolved
  UTF-8 text by the adapter (reusing the same `DriveLabel::driveDisplayLabel` helper and
  `GetDriveTypeW`/`GetVolumeInformationW` calls `StandardFileSystemRepository`'s `buildDriveNode()`
  already makes for "This PC", §14.15), so no OS-specific type crosses the Application boundary —
  same posture as `FileNode::displayName()`. `GetDiskFreeSpaceExW` supplies the byte counts.
  Failure (the "This PC" sentinel, an archive-virtual path, or an unreachable volume) is tolerated
  by simply blanking the label, same as `buildDriveNode()`'s `nullopt` case. `FileNavigationUseCase`
  and `TabViewModel` each add a thin passthrough (`TabViewModel::driveSpaceInfo()` calling
  `m_fileNavigationUseCase.driveSpaceInfo(currentPath())`), the same shape as existing
  single-Port-call wrappers (e.g. `showProperties`).
* **Refresh points:** `MainWindow` tracks the focused pane's active tab the same way it already
  does for sort/search (`m_sortTrackedTab`/`m_searchTrackedTab`) with a new
  `m_driveInfoTrackedTab`, connecting its `currentPathChanged` to re-fetch and reformat the drive
  label. Updates on tab-focus change and on navigation only — same event-driven-only posture as
  §14.28's own refresh, no timer/polling.
* **Threading:** kept synchronous on the UI thread, same as `buildDriveNode()`'s identical Win32
  calls during "This PC" enumeration (§14.15) and consistent with §5's accepted "most Port calls
  are still synchronous" gap.
* **Testing:** `driveSpaceInfo()`'s adapter implementation depends on live, per-machine drive
  state exactly like `buildDriveNode()` — covered by the same §11 carve-out ("live drive/known-
  folder enumeration... not covered by automated adapter tests"). The `FileNavigationUseCase`
  passthrough is covered by an ordinary mock-based test, per §11. `DriveLabel` is unchanged/reused
  as-is.
* **Not built:** live/periodic refresh while idle in the same folder; a populated right-hand
  section (layout reserves room for one); non-Windows implementations (a future Linux backend
  implements `driveSpaceInfo` behind the same Port, matching every other Windows-specific adapter
  call); any change to `StatusBarWidget` or `onStatusMessage`'s transient-message behavior.

### 14.32 Git Status Integration (Details View)

Per-file/folder git working-tree status badges in the Details-view file list, computed via
**libgit2** (the plain C library — not libqgit2, which isn't in vcpkg's registry; libgit2 is
wrapped by our own adapter, same posture as libarchive/libvips/FFmpeg/tree-sitter, so "use a
wrapper, never the library directly" is satisfied the same way it already is for those). One status
per file: `??` Untracked, `M` staged/unstaged (Modified), `A` staged (Added), `D` staged/unstaged
(Deleted), `!!` Ignored — seven `GitFileStatus` values total, each mapped to a small colored badge
icon (purple/blue/yellow/green/red/orange/grey respectively) rendered in a new column appended
after Date Modified.

* **New Port**, `IGitStatusProvider` (`src/application`) — kept separate from
  `IFileSystemRepository` (Interface Segregation, same reasoning that already keeps
  `IContextMenuProvider`/`IMediaDecoder` as their own Ports): `statusForDirectory(directory) ->
  Result<GitDirectoryStatus>`, where `GitDirectoryStatus` (`src/application/GitDirectoryStatus.h`,
  a plain Port-adjacent data type, same posture as `DriveSpaceInfo`) holds an optional repository
  root plus a `path -> GitFileStatus` map for `directory`'s immediate children only. `directory` not
  inside any repository is `Ok({nullopt, {}})`, not an error — same "tolerate by blanking" posture
  §14.31 established for `driveSpaceInfo`. **`GitStatusUseCase`** (new, stateless, depends only on
  `IGitStatusProvider&`) is a thin passthrough, same single-Port-dependency shape as
  `FilePreviewUseCase`.
* **`src/adapters/git`** (new module, `explorer_adapters_git`, mirrors `adapters/archive`):
  `LibGit2Repository` — the RAII wrapper around libgit2 itself (the only place its headers are
  included): `open(startDirectory)` discovers a repository walking upward (`Err` if none found —
  satisfies "in the current folder or a subfolder"), `statusForImmediateChildren(relativeDirectory)`
  runs `git_status_list_new` scoped to that directory via `options.pathspec` (so a large repo isn't
  fully re-walked on every navigation) with `GIT_STATUS_OPT_INCLUDE_UNTRACKED |
  GIT_STATUS_OPT_INCLUDE_IGNORED`, `RECURSE_UNTRACKED_DIRS` left off — an entirely-untracked
  subfolder is reported as itself (a single entry) rather than walked file-by-file, a real perf win
  in large repos. Status-flag → `GitFileStatus` mapping has an explicit priority order for a
  partially-staged file (worktree status wins over index status; see the plan record for the full
  list) since req. "one status per file" rules out showing both. `LibGit2StatusProvider` implements
  the Port on top of this, translating a "no repository found" `Err` into `Ok({nullopt, {}})` rather
  than propagating it, and **rolls a nested entry up onto its immediate-child ancestor directory**
  (the first path segment past `directory`) rather than dropping it — a modified file several levels
  deep stays visible without opening every intermediate folder. A folder that rolls up more than one
  nested change keeps its highest-priority one, via a rollup-specific priority order (its own
  ranking, not the flag-collapsing one above): Conflicted, Untracked, DeletedUnstaged,
  ModifiedUnstaged, DeletedStaged, AddedStaged, ModifiedStaged, Ignored last — Ignored sits at the
  bottom here specifically so an ignored build artifact next to a genuinely modified tracked file in
  the same folder doesn't hide the modification behind an "Ignored" badge on the folder row.
* **Threading** — one dedicated background `QThread` (`GitStatusService` +
  `GitStatusWorker`, `src/adapters/viewmodels`), same "one always-alive worker, not a pool" shape as
  `FilePreviewViewModel`/`WindowsConPtyProcess`; every open tab/pane's status requests serialize
  through it, sidestepping any libgit2 thread-safety question by construction (never touched from
  two threads at once) the same way `SQLiteTagRepository` avoids `SQLITE_BUSY` by serializing DB
  access (§5). Dispatch is `QMetaObject::invokeMethod` with a captured functor across both hops, not
  real Qt signals — same reasoning `FilePreviewWorker.h` documents for avoiding meta-type
  registration for non-default-constructible `Result<T>` payloads. `GitStatusService::requestStatus`
  takes a `QObject* context` and guards its reply-marshaling callback with a `QPointer<QObject>`, so
  a request outliving its requester (e.g. a closed tab) is silently dropped rather than dereferencing
  a dangling `this`.
* **Dispatch point is `TabViewModel`, not `FileListModel`** — `FileListModel` gains no new
  constructor dependency, only a plain `setGitStatuses(...)` slot (storing the map, emitting
  `dataChanged` for the new column), keeping it decoupled from git entirely, the same posture
  `resolveDecoration()` keeps it decoupled from `FileDecorationsViewModel`'s concrete type (§14.30).
  `TabViewModel` gains a `GitStatusService&` constructor dependency (threaded through
  `WorkspacePaneViewModel`/`WorkspaceController` exactly like `TagManagementUseCase&`) and dispatches
  a request on every `directoryContentsChanged` — the single funnel point `loadAndApply` already
  provides for every navigation entry point, including the existing `refresh()` used after in-app
  file operations, so a status refresh after e.g. a paste comes for free with no new wiring. Only the
  tab's primary browsing `FileListModel` is wired this way — the two search-result `FileListModel`
  instances never receive `setGitStatuses()`, which is how quick-search/advanced-search are excluded
  (below) with no extra flag.
* **`FileListModel`**: new `Column::GitStatusColumn`, appended after `DateModifiedColumn` (no
  existing column index shifts). "Details view only" falls out for free from the existing
  `QTreeView`/`QListView` split (§2.3.1) — `QListView`'s icon/list/tile modes already only read
  column 0. `data()` returns `{}` (no badge) for any entry not present in the status map; a hit
  returns a cached 16×16 badge icon (`Qt::DecorationRole`, one per `GitFileStatus` value, built
  once) plus a human-readable `Qt::ToolTipRole` string. Synthetic rows are skipped, same as
  decorations.
* **Not built:** status in quick-search/advanced-search result views (those `FileListModel`
  instances span arbitrary directories, which doesn't fit a per-directory status query — a
  deliberate v1 scope cut); rollup past the immediate-child level (a folder's badge reflects
  changes anywhere in its subtree, but a grandchild folder itself gets no separate entry unless
  `directory` is queried directly for it); live/background refresh outside of navigation (no
  filesystem or `.git`-index watcher — matches
  §14.31's "event-driven only" precedent); rename detection (no "R" status in scope); any stage/
  unstage/commit/other write action — read-only display only. No automated coverage for the
  `FileListModel`/`TabViewModel`/`GitStatusService` glue, per the same §11 UI/ViewModel-glue
  carve-out `FilePreviewViewModel`/`DriveSpaceInfoWorker` already fall under; `GitStatusUseCase` is
  covered by an ordinary mock-based test and `LibGit2StatusProvider` by a real-temp-repo adapter
  test (initialized via libgit2's own `git_repository_init`, not a `git` CLI shell-out), per §11.

### 14.33 Live Directory Update (Visible Tabs, Non-Recursive)

Automatically reflects a file being created/deleted/modified in a browsed folder without the user
pressing Refresh — scoped to the tabs currently **shown** (the active tab of each pane the current
`SplitLayout` makes visible: 1 in Single, 2 in a split, 4 in the grid) and to that folder's
**immediate children only**, never a nested subdirectory.

* **New Port**, `IDirectoryWatcher` (`src/application`) — kept separate from
  `IFileSystemRepository`, same Interface Segregation reasoning as `IGitStatusProvider`/
  `IContextMenuProvider`/`IMediaDecoder`: `watch(directory, std::function<void()> onChanged) ->
  Result<DirectoryWatchId>` (multiple independent watches on the same directory are allowed) /
  `unwatch(DirectoryWatchId)`. A non-real path (the "This PC" sentinel, an archive-virtual path, a
  path that no longer exists) surfaces as a `Result` failure rather than crashing; the caller
  simply doesn't get live updates for that tab (same "tolerate by blanking" posture as
  `driveSpaceInfo`, §14.31). **`DirectoryWatchUseCase`** (new, stateless, single-Port-dependency
  passthrough, same shape as `GitStatusUseCase`).
* **`src/adapters/filesystem`** gains `StandardDirectoryWatcher` (implements the Port on top of
  one owned `QFileSystemWatcher` — chosen over hand-rolling `ReadDirectoryChangesW` because it's
  already non-recursive per directory by construction, and its Windows backend watches
  name/attribute/size/last-write changes of immediate children, covering create/delete/rename/
  modify for free; it also gives a future Linux port (inotify backend) for free) and
  `DirectoryWatchRegistry` (pure C++, no Qt — refcounted path→watcher-callbacks bookkeeping
  deciding when to actually start/stop the native watch — same "pure logic factored out of OS
  glue" split as `AnsiTerminalBuffer`/`WindowsConPtyProcess`, GTest-covered). Path conversion goes
  through `.wstring()` → `QString::fromStdWString(...)`, never `.string()`/`PathUtf8::toUtf8()`
  (§6). This is `adapters/filesystem`'s first Qt dependency (`Qt6::Core`, for
  `QFileSystemWatcher`/`QObject`) — `StandardFileSystemRepository`/`DriveLabel` use plain
  `std::filesystem`/Win32 only.
* **`DirectoryWatchService`** (new, `src/adapters/viewmodels`) — owned long-lived by
  `WorkspaceController` (constructed internally, same posture as `TagListViewModel`/
  `FilePreviewViewModel`/`FileOperationsController`). `setVisibleTabs(tabs)` diffs against the
  currently-watched set: newly-visible tabs are watched *and* refreshed once immediately to catch
  up on anything missed while unwatched (mirrors `FilePreviewViewModel::setPanelActive(true)`'s
  reactivate-and-catch-up posture); no-longer-visible tabs are unwatched. Each watched tab tracks
  its `DirectoryWatchId`, a ~400ms debounce `QTimer` (coalesces a burst of native change events —
  e.g. a multi-file copy — into one refresh), and connections to that tab's `currentPathChanged`
  (re-point the watch at the new directory, same tab stays watched) and `destroyed()` (cleanup on
  tab close). A debounced notification calls the existing `TabViewModel::refresh()` slot (§14.10)
  — no new refresh path. Only the normal-browsing `FileListModel` is affected; a watched tab
  currently showing quick/advanced-search results still refreshes its (hidden) primary listing in
  the background, same scope cut §14.32 already made.
* **`WorkspaceController`** gains a `DirectoryWatchUseCase&` constructor parameter and a private
  `updateLiveWatchedTabs()` (`WorkspaceLayoutTopology::visiblePanes(m_layout)` → each visible
  pane's `activeTab()` → `DirectoryWatchService::setVisibleTabs`), called after construction, from
  `setLayout()`, and from the existing per-pane `activeTabChanged` connection loop — deliberately
  **not** from `focusedPaneChanged`, since visibility (not focus) decides what's watched: all 4
  panes' active tabs live-update under `FourGrid` even though only one pane has focus.
* **Threading** — none new. Registering/unregistering a directory watch is a fast, non-blocking
  syscall, same "cheap OS call stays synchronous on the UI thread" posture as `driveSpaceInfo`
  (§14.31)/`ShellExecuteW` (§14.11); `QFileSystemWatcher` delivers notifications through Qt's own
  event loop with no manual thread. Unlike git-status (a real disk/`.git` scan, needing
  `GitStatusService`'s background thread), there is no scan to offload here.
* **Known limitation:** `refresh()`'s `loadAndApply()` already clears the current selection and
  fully resets `FileListModel` (`beginResetModel()`/`endResetModel()`) on every call — pre-existing
  behavior for the manual Refresh button and the post-file-op refresh (§14.10), now also triggered
  by external changes, more frequently and more disruptively than before (Explorer itself
  preserves selection across a live update). Selection-preserving refresh is a real improvement but
  a separate, larger change to `FileListModel`/`loadAndApply`; deliberately out of scope here.
* **Not built:** watching inactive tabs or hidden-pane tabs (they simply go stale until shown or
  manually refreshed); live updates inside quick-search/advanced-search result views; a Linux
  `IDirectoryWatcher` implementation (deferred until Linux support is actually built, same posture
  as every other Windows-first Port). No automated coverage beyond `DirectoryWatchRegistry`
  (pure bookkeeping) and `DirectoryWatchUseCase` (mocked-Port passthrough) — `WorkspaceController`/
  `DirectoryWatchService`/`StandardDirectoryWatcher` fall under the existing §11 UI/ViewModel-glue
  and OS-shell carve-outs, smoke-tested manually.
