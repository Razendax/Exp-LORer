# Architecture Document — Feature Decision Log: Preview & Decoration

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.26–14.27, §14.29–14.30, §14.58–14.59, §14.67 — the right-panel file Preview tab, its
text-preview syntax highlighting, archive browsing/preview/extraction, per-file/folder font &
color decoration, the Preview panel's dedicated per-kind widgets and media-layout rework, and the
built-in Widgets video player (first used here, before Media Viewer mode reused it). Section
numbers are **not** contiguous with this file's neighbors — they were assigned chronologically as
each feature was built and are kept stable since code comments cite them directly. See
[Architecture.md](../Architecture.md) §14 for the full topic index across all nine files.

---
### 14.26 Right-Panel File Preview (Preview Tab)

A second `RightPanelWidget` tab, "Preview" (§14.25's panel already supports more than one tab with
no further plumbing), showing content for whatever the focused pane/tab currently has selected —
or its browsed folder when nothing is selected, same resolution rule `TagListViewModel::
resolveTarget()` already uses for the tag panel (§14.9). This is also the first concrete
implementation of `adapters/media` (previously `.gitkeep` only) and the first feature to actually
do background-thread Port dispatch (§5).

* **Scope by file type:** a directory shows its immediate children only (directories first, then
  files, both alphabetical — not a recursive tree); a recognized text/source extension
  (txt/html/xml/json/md/ini/log/yaml/csv, common source extensions cs/cpp/h/hpp/c/py/js/ts/css/…)
  shows a capped byte-prefix of its content, falling back to "unsupported" if a `\0` appears in
  that prefix (binary guard); an image extension (jpg/png/bmp/gif/webp) shows a scaled preview
  decoded via **libvips** (new vcpkg dependency), per the product requirement to use that library
  specifically rather than Qt's own image codecs; a video extension (mp4/mkv/avi/mov/…) shows a
  scaled **static poster frame** decoded via FFmpeg (already a dependency) — no inline playback,
  which belongs to the still-unbuilt full-window QML viewer (Specification §2.3/§2.4) instead.
  Anything else shows "no preview available."
* **`FilePreviewUseCase`** (new, `src/application`) — stateless, depends on
  `IFileSystemRepository` (folder listing via the existing `listDirectory`; a new
  `readFilePrefix(path, maxBytes)` method for text) and `IMediaDecoder` (image/video thumbnail
  bytes, reusing the existing `generateThumbnail(path, maxWidth, maxHeight)` contract) — never on
  `MediaProcessingUseCase` or any other use case, preserving this layer's "depend on Ports only"
  testability posture (§11). Returns a new Domain entity, `FilePreview` (kind + folder
  entries/text/image-or-poster bytes).
* **`adapters/media`** (new target `explorer_adapters_media`): `MediaDecoder` — the concrete
  `IMediaDecoder` dispatching by extension to `VipsImageDecoder` (images, libvips end to end:
  decode, resize, JPEG-encode) or `FFmpegMediaDecoder` (video, one representative frame via
  libavformat/libavcodec, scaled via libswscale, JPEG-encoded via libavcodec's MJPEG encoder) —
  each self-contained, not cross-calling the other, keeping the two backends independently
  swappable. `ThumbnailCache` + `CachingMediaDecoder` (§8) sit in front as a transparent decorator,
  so `FilePreviewUseCase` never sees caching logic.
* **`FilePreviewViewModel`** (new, `adapters/viewmodels`) — the shared ViewModel, owned by
  `WorkspaceController` and retargeted via `setActiveTab()` exactly like `TagListViewModel` (§14.9)
  — a **distinct component from the still-unbuilt `MediaPreviewViewModel`** named in §2.3, which is
  QML-facing state for the future full-window viewer, not this panel; the two must not be
  conflated. Owns one dedicated background `QThread` + `FilePreviewWorker` (one always-alive
  worker, not a pool — only one active preview needed at a time), the same "one background thread,
  results marshaled back via a queued signal" shape `WindowsConPtyProcess` already established
  (§14.24). A `setPanelActive(bool)` slot, driven by the widget's `showEvent`/`hideEvent`, stops
  background work while the Preview tab isn't the visible one and catches up immediately on
  reactivation. Cancellation is a generation-id fence (§5) — switching the selection bumps a
  counter, and a result that arrives for a stale generation is dropped, never shown; the
  in-flight libvips/FFmpeg call itself still runs to completion, since neither library exposes a
  mid-call abort. A small in-memory LRU (~20 entries) caches `Text`/`Folder` results only —
  image/video results are already cached on disk by `CachingMediaDecoder`, so caching them a
  second time here would be redundant.
* **`PreviewPanelWidget`** (new, `src/ui/widgets`) — a `QStackedWidget` switching between an image/
  poster `QLabel`, a read-only monospace `QPlainTextEdit` for text, a flat `QListWidget` (existing
  `QFileIconProvider` icons) for the one-level folder listing, and loading/unsupported/error
  states, driven by `FilePreviewViewModel`'s signals. `MainWindow::createWorkspace` adds it as a
  second `RightPanelWidget` tab next to "Tags" — no change needed to `RightPanelWidget` itself.
* **Not built:** video playback; a recursive/multi-level folder tree; animated-GIF playback beyond
  a static first frame (goes through the image path); full-content display beyond the byte cap
  (syntax highlighting of the shown prefix is covered by §14.27); a Linux media-decoding backend.
  Persisting which panel tab (including this one) is active and expanded is covered by §14.48,
  superseding the "matches §14.24's existing unpersisted posture" note this bullet previously had
  for the Preview tab specifically — the bottom panel (§14.24) itself remains unpersisted.
* **Image/video info + zoom label:** `FilePreviewUseCase` now also calls
  `IMediaDecoder::extractMetadata()` for Image/Video targets (alongside the existing
  `generateThumbnail()` call), and `FilePreview::image()`/`video()` carry the target's path plus
  the resulting `MediaMetadata` (width/height, and bit rate for video via FFmpeg's
  `AVFormatContext::bit_rate`) — a zero-valued `MediaMetadata` on extraction failure rather than
  failing the whole preview, same graceful-degradation posture as the text path's binary guard.
  `PreviewPanelWidget`'s image/poster stack page gained two labels around the existing pixmap
  label: an info line (fullname + resolution, + bit rate for video) and a bottom zoom line
  showing the displayed pixmap size as a percentage of the file's actual resolution, recomputed
  whenever the panel resizes.

*Superseded in part by §14.58:* `PreviewPanelWidget` no longer owns each kind's Qt widgets
directly — it now delegates to three dedicated per-kind widgets (`MediaPreviewWidget`,
`TextPreviewWidget`, `FolderPreviewWidget`), added so that a future preview kind can be introduced
as one new self-contained widget rather than by editing this shared file's constructor/member
list/switch statements. Pure internal restructuring; the behavior and bullets above are otherwise
unchanged.

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

### 14.58 Preview Panel: Dedicated Per-Kind Widgets

Pure internal restructuring of `PreviewPanelWidget` (§14.26) — no behavior change, no Domain/
Application/Port change. Before this, `PreviewPanelWidget` built and owned every preview kind's Qt
widgets directly (an image/poster composite page, a `QPlainTextEdit`, a `QListView` +
`FolderPreviewListModel`, plus loading/unsupported/error `QLabel`s) and held all the per-kind
rendering logic (`showImage`/`showText`/`showFolder`/`rescaleImageLabel`) itself, dispatched from
one `onPreviewReady()` switch. Extending that file for a future preview kind meant editing its
constructor, member list, and switch statements all at once.

* **`MediaPreviewWidget`** (new, `src/ui/widgets`) — a composite `QWidget` (info `QLabel` + pixmap
  `QLabel` + zoom `QLabel`, same three-label layout the old image page used) covering both
  `FilePreviewKind::Image` and `::Video`, since their rendering is otherwise byte-identical (same
  JPEG-decoded poster bytes; video only adds a bitrate suffix to the info line). Exposes
  `bool setPreview(const FilePreview&)`, returning `false` on a decode failure (null pixmap) so the
  caller falls back to its own shared "unsupported" label rather than this widget needing to know
  that label exists. Owns its own `resizeEvent` override to rescale its pixmap/recompute the zoom
  label — `PreviewPanelWidget::resizeEvent` is gone entirely now that nothing else needs it.
* **`TextPreviewWidget`** (new) — subclasses `QPlainTextEdit` directly rather than wrapping one,
  the same "dedicated widget that just *is* a specialized Qt view/edit control" pattern
  `FolderTreeView` (§14.41) already established for `QTreeView`. Owns its own `SyntaxHighlighter`
  (§14.27), constructed from the `SyntaxHighlightEngine&`/`HighlightThemeViewModel&` it now takes
  directly instead of `PreviewPanelWidget` plumbing them through. Exposes `setPreview(const
  FilePreview&)`.
* **`FolderPreviewWidget`** (new) — subclasses `QListView` directly, same pattern; owns its own
  `FolderPreviewListModel` (unchanged). Exposes `setPreview(const FilePreview&)`.
* **`PreviewPanelWidget`** is now a thin coordinator: constructs the three widgets above plus the
  three status `QLabel`s, and `onPreviewReady()` just calls the matching widget's `setPreview()`
  and switches the stack to it (falling back to the unsupported label when `MediaPreviewWidget::
  setPreview` returns `false`). All objectNames (`previewImageLabel`, `previewImageInfoLabel`,
  `previewZoomLabel`, `previewTextView`, `previewFolderList`, `previewLoadingLabel`,
  `previewUnsupportedLabel`, `previewErrorLabel`, `previewPanelWidget`) are unchanged, so
  `ui_tests/framework/automation_id.py` needed no edits.
* **Not built / changed:** no new preview kind, no `FilePreviewViewModel`/`FilePreviewUseCase`/
  `FilePreview` change — this section is UI-layer only. `MainWindow`'s construction of
  `PreviewPanelWidget` is unchanged (same public constructor signature).
* **Testing**: no CTest coverage, consistent with §11's "UI is not covered by CTest in v1" — same
  carve-out the original §14.26 widget already fell under. `ui_tests/` needed no changes since
  every automation id resolves by objectName + Qt widget type, both unchanged.

### 14.59 Preview Panel: Media Layout Rework (Name/Image/Properties/Zoom) + Smaller Folder Icons

`MediaPreviewWidget` (§14.58) packed the filename, resolution, and (for video) bitrate into one
info line above the image, and centered the image both horizontally and vertically in the space
below it. Reworked into four clearly separated rows, top to bottom: the full filename alone, the
poster/thumbnail (top-aligned, `Qt::AlignHCenter | Qt::AlignTop`, no longer vertically centered), a
properties block, and the unchanged zoom label.

* **`MediaPreviewWidget`**: `m_infoLabel` (objectName `previewImageInfoLabel`) split into
  `m_nameLabel` (`previewImageNameLabel`, filename only, bold and two points larger than the
  default label font) and `m_propertiesLabel` (`previewImagePropertiesLabel`, one line each for
  Resolution, Length (`::Video` only, `metadata.duration()` formatted `mm:ss`/`hh:mm:ss`), Bitrate
  (`::Video` only), and Size, each shown only when the underlying value is known/positive), the
  latter now wrapped in a `QGroupBox` titled "Properties"
  (`previewImagePropertiesGroupBox`) instead of sitting bare in the layout. `m_imageLabel`'s
  alignment changed from `Qt::AlignCenter` to `Qt::AlignHCenter | Qt::AlignTop`.
  `ui_tests/framework/automation_id.py` defines neither old nor new info-label/group-box id (only
  `PREVIEW_IMAGE_LABEL`), so no edit needed there.
* **Shrink responsiveness fix**: `m_imageLabel` previously kept Qt's default size policy, so its
  `sizeHint()`/`minimumSizeHint()` tracked whatever pixmap `rescaleImage()` had last set (i.e. the
  prior, larger panel size) — shrinking the right panel's splitter couldn't make incremental
  progress against that stale hint and instead jumped straight to the overall minimum on release.
  Expanding was unaffected since a growing hint never blocks growth. Fixed by setting
  `QSizePolicy::Ignored` on both axes plus an explicit `setMinimumSize(1, 1)`, so the layout no
  longer consults the label's size hint at all and `resizeEvent()` → `rescaleImage()` tracks the
  splitter continuously in both directions.
* **Fixed-width fix**: `m_nameLabel`/`m_propertiesLabel` kept Qt's default (`Preferred`, which has
  `ShrinkFlag` set) horizontal size policy, so per `qSmartMinSize()` the layout's floor for each was
  `minimumSizeHint().width()` — for a word-wrapped `QLabel`, that's the width of its widest
  *unbreakable* run of characters. A long filename (or property value) with no spaces can't wrap at
  all, so its minimum width was its full unwrapped text width, forcing the whole right panel/splitter
  wider whenever such a file was previewed, and it never shrank back (splitters only grow to satisfy
  a rising minimum). Fixed by setting the horizontal policy on both labels to `QSizePolicy::Ignored`
  (vertical policy unchanged), which drops that floor to 0 while word-wrap/`heightForWidth` still
  reflow the text correctly within whatever width the layout actually hands it.
* **`FilePreview`** (Domain, additive): `image()`/`video()` now take a `std::uintmax_t
  fileSizeBytes` parameter, exposed via new accessor `fileSizeBytes()` (0 for every other kind) —
  the same graceful-degradation posture as the rest of the entity, not a reshape of its existing
  shape. `FilePreviewUseCase::generatePreview()` threads `target.size()` (already stat'd as part of
  the `FileNode` passed in — no new filesystem call) through to both factory calls.
* **`FolderPreviewWidget`** (§14.26): now calls `setIconSize(QSize(16, 16))` and
  `setUniformItemSizes(true)` on construction — folder-preview rows previously used
  `QListView`'s unset (style-default, visually large) icon size; 16×16 matches the icon size
  `FileBrowserView` already uses for its own `ViewMode::List`/`::Details`.
* **Not built/changed**: no new `FilePreviewKind`, no `IMediaDecoder`/`IFileSystemRepository`
  change, no `FilePreviewViewModel` change.
* **Testing**: `tests/domain/FilePreviewTest.cpp` and
  `tests/application/FilePreviewUseCaseTest.cpp` updated for the new `fileSizeBytes` parameter/
  accessor. No UI test changes (automation ids above unaffected); UI is still not covered by CTest
  in v1 (§11).

### 14.67 Simple Built-In Video Player (Widgets, not QML)

Specification.md §2.4 and the original Architecture.md sketch called for a full-window QML viewer
(`MediaPreviewViewModel`, via `QQuickWidget`) for video playback, but no QML file was ever added to
the project -- every other media feature (poster frames, grid hover previews, §14.26/§14.58/
§14.64) shipped in plain Qt Widgets instead. Rather than introduce the project's first QML surface
and `Quick`/`Qml`/`QQuickWidget` CMake plumbing for what the user wants kept simple, the player is
plain Qt Widgets + Qt Multimedia (`QMediaPlayer`/`QVideoWidget`/`QAudioOutput`), consistent with the
rest of the codebase. `qtmultimedia`'s vcpkg port already default-builds the `widgets` feature and
the `ffmpeg` decode backend, so no `vcpkg.json` change was needed -- only `find_package(Qt6 ...
Multimedia MultimediaWidgets)` at the top level and linking those two modules into
`explorer_ui_widgets`.

* **No new Port.** `QMediaPlayer` loads the video file directly from `FilePreview::path()`; there is
  no business logic to express as a use case, so `VideoPlayerWidget` talks to Qt Multimedia
  directly with no `IVideoPlayer`-style abstraction -- same posture as `TerminalWidget`/
  `WindowsConPtyProcess` (Architecture.md §9/§10): a pure OS/Qt integration with no second
  implementation ever in scope, so an interface would buy nothing.
* **Standalone component** (`src/ui/widgets/video/VideoPlayerWidget`), not nested under `preview/`:
  the user explicitly wants it reusable later (e.g. Media Viewer grid playback, a future
  full-window viewer) without having to move code out from under an unrelated feature's folder
  first. `PreviewPanelWidget` is just its first consumer -- the same relationship `preview/`
  already has with `SyntaxHighlightEngine`/`HighlightThemeViewModel`.
* **UI shape**: `QVideoWidget` on top (stretches); a controls row below with a play/pause toggle
  `QToolButton` (`SP_MediaPlay`/`SP_MediaPause`), a separate Stop `QToolButton`
  (`SP_MediaStop`, distinct from pause per the user's request), and a `mm:ss / mm:ss` time label;
  a bottom row with the seek `QSlider` (stretch), a mute-toggle `QToolButton`
  (`SP_MediaVolume`/`SP_MediaVolumeMuted`), and a volume `QSlider` (0-100, mapped to
  `QAudioOutput::setVolume()`'s 0.0-1.0). All icons are Qt standard icons -- no new icon assets.
* **Seek-drag handling**: while the seek slider is pressed (`sliderPressed` through
  `sliderReleased`), incoming `positionChanged` updates are suppressed so playback doesn't fight the
  drag; `sliderMoved` live-updates the time label; `sliderReleased` commits via
  `player->setPosition()`.
* **Public API**: `load(path)` sets the source (`QUrl::fromLocalFile(QString::fromStdWString(
  path.wstring()))` -- never `.string()`/`.generic_string()`, per CLAUDE.md's non-ASCII-path rule)
  and leaves playback **paused** (first frame visible, no autoplay/audio burst on selection).
  `release()` stops playback and clears the source; called from every point the widget can stop
  being the visible content -- `PreviewPanelWidget::hideEvent`, `onPreviewLoading`,
  `onPreviewFailed`, `onPreviewCleared` (unconditionally, since a no-op release is harmless), and
  whenever `onPreviewReady` resolves to a non-Video kind or the mode combo switches away from the
  player -- so playback/audio never continues once the Preview tab is hidden, the selection changes,
  or a newer preview generation supersedes it.
* **Preview tab integration**: `PreviewPanelWidget` gains a `QComboBox` ("Static image" / "Video
  player") shown only while the current preview's kind is `Video`, plus `VideoPlayerWidget` as a
  new `m_stack` page. The last-seen Video `FilePreview` is kept as a member
  (`std::optional<FilePreview>`) so switching the combo re-renders through the newly selected page
  without re-asking `FilePreviewViewModel`; "Static image" renders through the existing
  `MediaPreviewWidget::setPreview()` (same poster-frame path image/video already shared), "Video
  player" calls `VideoPlayerWidget::load()`. The mode choice is a plain member, not persisted via
  `AppConfig`/`QSettings` -- not requested, and avoids config/schema churn for a "simple" player.
* **Deployment**: the FFmpeg and Windows Media Foundation playback-backend plugin DLLs
  (`ffmpegmediaplugin.dll`, `windowsmediaplugin.dll`, targets `Qt6::QFFmpegMediaPlugin`/
  `Qt6::QWindowsMediaPlugin`) are loaded by Qt Multimedia at runtime from a `multimedia/` subfolder
  next to the executable -- vcpkg's applocal DLL copy doesn't reach plugin-loaded DLLs, so
  `src/app/CMakeLists.txt` deploys them explicitly, same pattern as the existing `platforms/`/
  `sqldrivers/`/`imageformats/` post-build steps. No separate Windows audio-backend plugin exists
  (WASAPI is compiled in).
* **Not built** (Specification.md §2.4 lists these; not requested now): fullscreen toggle,
  aspect-ratio adjustments, loop playback; any playback in the Media Viewer grid/gallery mode
  (stays poster + hover-preview only, §14.62/§14.64/§14.66); persisting the mode choice across
  sessions; any change to the `IMediaDecoder`/`FFmpegMediaDecoder`/thumbnail-cache pipeline, which
  keeps serving the poster frame and grid hover previews exactly as before.
* **Testing**: no new GTest coverage -- pure UI-layer addition, no Domain/Application/Adapter
  change, and UI stays uncovered by CTest in v1 (Architecture.md §11); manual smoke test only.

