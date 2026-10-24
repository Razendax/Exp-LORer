# Architecture Document — Feature Decision Log: Preview & Decoration

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.26–14.27, §14.29, §14.58–14.59, §14.67, §14.71, §14.76, §14.79, §14.81, §14.84–14.85, §14.98, §14.100–14.101, §14.106 — the right-panel file Preview tab, its
text-preview syntax highlighting (plus its later expansion to YAML/PowerShell/Markdown, §14.100),
archive browsing/preview/extraction, the Preview panel's dedicated per-kind widgets and media-layout rework, the
built-in Widgets video player (first used here, before Media Viewer mode reused it), real
image/video previews in the main browser's Extra Large Icons view, the mouse-hover preview
popup, GIF animation / SVG rendering support across both, a GIF-thumbnail-decode bug fix, a
checkerboard background fix for transparent SVG/GIF previews in both of those surfaces, a
content-only mode-switch/collapsible-properties rework of the Preview tab's media chrome,
replacing the text page's bare read-only view with the built-in text editor (Edit/Revert/Save),
and PDF preview support (§14.101). Per-file/folder font & color decoration, formerly also covered
here, moved to [features/11-decoration.md](11-decoration.md) once this file approached §14's
1000-line-per-file cap. Section numbers are **not** contiguous with this file's neighbors — they were assigned chronologically as
each feature was built and are kept stable since code comments cite them directly. See
[Architecture.md](../Architecture.md) §14 for the full topic index across all files.

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

*Superseded in part by §14.100:* the language scope recorded below (C/C++/C#/Python/JS/TS/JSON/
HTML/CSS, with YAML/Markdown/PowerShell left as plain text) reflects this feature as first built;
§14.100 adds YAML, PowerShell, and Markdown (block-level) to that scope. The mechanism and
everything else below is otherwise unchanged.

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

### 14.71 Extra Large Icons: Real Image/Video Previews

Extended `ViewMode::ExtraLargeIcons` (the main file browser's 256×256 icon-grid tier) to show a
real decoded preview for image/video files instead of the generic Shell icon every other icon-size
tier still shows — reusing Media Viewer grid's existing thumbnail pipeline (§14.62) rather than
building a second one: **no Domain/Application/Port change**, pure UI/Adapters wiring.

* **Reused unchanged**: `FileListModel::MediaThumbnailRole`/`setMediaThumbnails()`/
  `m_mediaThumbnailsByPath` (already generic, keyed by path, with no Media-Viewer-specific
  coupling), `MediaThumbnailLoadingService::requestThumbnails()`, `MediaThumbnailData`, and
  `MediaThumbnailPainting::paintThumbnail`/`paintPlaceholderIcon`.
* **`FileIconDelegate`** (`src/ui/widgets/navigation`) gained a `setViewMode(ViewMode)` setter;
  `paint()` now reads `MediaThumbnailRole` and paints via `MediaThumbnailPainting::paintThumbnail`
  (aspect-fit, letterboxed, same as `MediaGridTileDelegate`) when `m_viewMode ==
  ExtraLargeIcons` and the row is a non-directory image/video file (`MediaExtensions::
  isImageExtension`/`isVideoExtension`), falling back to the plain Shell icon otherwise/until the
  real thumbnail arrives. The delegate's own local placeholder-icon painter was replaced by
  `MediaThumbnailPainting::paintPlaceholderIcon` (functionally identical — largest available
  `QIcon` pixmap stretched to fill the cell) to drop a duplicate implementation.
  `FileBrowserView::setViewMode()` forwards the active mode to the delegate.
* **`TabViewModel`** gained `requestBrowsingThumbnails(path, entries)`, mirroring
  `requestMediaViewerResultThumbnails`'s shape but pre-filtering `entries` to non-directory image/
  video files first (the browsing model's entries aren't media-only like Media Viewer's
  candidates already are) before dispatching to `MediaThumbnailLoadingService` at 256×256/`High`
  priority. Called from the same two places `requestIcons(path, entries)` already is — the
  `directoryContentsChanged` funnel and `setViewMode()` — gated to `ViewMode::ExtraLargeIcons`
  only. `setViewMode()`'s existing early-return-on-unchanged-`IconSizeMask` short-circuit (every
  icon-grid tier resolves to the same `IconSizeMask::Large`, so it never fired for a tier-to-tier
  switch) does not gate this new dispatch — entering Extra Large Icons from any other tier always
  (re)requests thumbnails for the already-listed entries, same as a fresh directory listing does.
  A request for an already-decoded path is cheap: `CachingMediaDecoder`/`ThumbnailCache` (§8)
  short-circuit on a cache hit without re-invoking FFmpeg/libvips.
* **Deliberately scoped to this one tier**: Large/Medium/Small Icons, Tiles, List, and Details
  keep the plain Shell icon, matching the request's own "extra large" wording and Explorer's own
  behavior (only its largest icon tier shows real thumbnails). Quick Search and Advanced Search
  results panes are unaffected — both force Details/tree rendering regardless of `ViewMode`, so
  Extra Large Icons never applies to `m_searchResultsModel`/`m_advancedSearchResultsModel`.
* **Not built**: video hover-scrub preview frames (`MediaPreviewFrames`) for this view — that
  stays a Media-Viewer-grid-only feature (§14.64); any change to Media Viewer mode itself.
* **Testing**: no new GTest coverage — pure UI/Adapters wiring reusing an already-tested pipeline,
  same carve-out Media Viewer's own grid thumbnail delegate falls under (§11, §14.62).

---
### 14.76 Hover Preview Popup for Images/Videos

A frameless popup, anchored to the right of the mouse cursor, showing an image/video file's
preview while the mouse hovers its row in the normal file browser (not Media Viewer Mode, which
already owns the grid's own per-tile hover-scrub, §14.64) — lets the user preview a file without
selecting or opening it. A third, independent consumer of the already-shared decode pipeline; no
Domain/Application/Port or schema change.

* **`TabViewModel::requestHoverPreviewThumbnail(entry, maxWidth, maxHeight, onReady)`** — a thin
  wrapper over `MediaThumbnailLoadingService::requestThumbnails`, mirroring
  `requestBrowsingThumbnails`/`requestMediaViewerResultThumbnails`'s shape. Two deliberate
  deviations from those: it passes `this` (the `TabViewModel`) as the service's `context`, not
  `m_fileListModel` — a shared context would let a hover request purge or be purged by an
  unrelated in-flight grid-icon request for the same model (`MediaThumbnailLoadingService`'s
  per-context `High`-priority purge, §14.62); and it tags the request's `root` with the hovered
  file's own path, not a containing directory — `root` is only ever compared for equality, so this
  repurposing makes the service's existing staleness check double as hover's own
  "superseded by a newer hover target" cancellation, with no new plumbing. A decode already
  in-flight still runs to completion (no decoder here supports true mid-call abort, §14.26) but
  simply isn't delivered to a stale target.
* **`MediaHoverPreviewPopup`** (new, `src/ui/widgets/navigation`) — a `QWidget` with
  `Qt::ToolTip | Qt::FramelessWindowHint`, `WA_ShowWithoutActivating`, and (unlike the similar
  `AddressBarWidget`/`TagSearchEdit` popups, which are interactive lists) `WA_TransparentForMouseEvents`
  and `Qt::NoFocus`, since this one is purely informational. Shows a single static frame
  (`previewIndex` 0; no scrubbing) scaled to fit a fixed max size, positioned beside the cursor via
  `QGuiApplication::screenAt()`'s geometry, flipping to the cursor's left/clamping vertically near
  a screen edge.
* **`FileBrowserView`** — mouse tracking, previously enabled only for `m_listView`'s viewport in
  `DisplayMode::MediaViewerGrid` (for hover-scrub, §14.64), is now enabled unconditionally on both
  `m_listView`/`m_treeView` viewports. A new `eventFilter` branch (gated on
  `m_displayMode != DisplayMode::MediaViewerGrid`, so it never fires for the Media-Viewer-grid
  instance) resolves the row under the cursor on `MouseMove`/`Leave`, classifies it via the
  already-shared `MediaExtensions::isImageExtension`/`isVideoExtension`, and — if the hover target
  changed and the feature is enabled — (re)starts a ~350ms single-shot debounce timer before
  emitting `mediaHoverPreviewRequested(entry)`. `showHoverPreviewResult(path, image)` discards a
  reply whose path no longer matches the currently-tracked target. The popup is also hidden
  immediately on `Leave` or any mouse button press (drag/click).
* **Enable/disable**: one checkable `QAction` ("Enable Hover Preview") under a new `View ▸ Hover
  Preview` submenu, following `MainWindow::createContextMenuModeAction`'s simplest existing
  pattern — a plain `QSettings`-backed flag (`FileBrowserView::kHoverPreviewEnabledSettingsKey`/
  `hoverPreviewEnabled()`) read fresh on every hover check, no live propagation across
  panes/tabs needed.
* **Wiring**: `WorkspacePaneWidget::wireBrowserView` (already called unconditionally for all
  three of a tab's `FileBrowserView` instances) connects `mediaHoverPreviewRequested` to
  `TabViewModel::requestHoverPreviewThumbnail`, forwarding the result back to the originating
  `FileBrowserView::showHoverPreviewResult` — same "wire unconditionally, fires only where
  applicable" posture already used for `mediaViewerDetailsTagClicked`.
* **Not built**: separate image/video toggles (one flat enable/disable flag); reopening §14.71's
  "no hover-scrub outside Media Viewer grid" boundary (this popup never scrubs); any interaction
  with the popup itself; any change to the Preview panel (§14.26) or Media Viewer's own hover-scrub
  (§14.64).
* **Testing**: no new GTest coverage — same UI/ViewModel-glue carve-out as §14.71 above.

---
### 14.79 GIF Animation & SVG Rendering

Supersedes §14.26's "Not built: ... animated-GIF playback beyond a static first frame" note: GIFs
can now be played animated in both the Preview tab and the hover popup. Adds the first real SVG
support anywhere in the app (previously unrecognized by `MediaExtensions` entirely). No Port,
Domain, or Application interface change — `.svg` is handled by a new concrete `IMediaDecoder`
implementation composed into the existing `MediaDecoder`, the same isolation pattern
`VipsImageDecoder`/`FFmpegMediaDecoder` already established (§14.26's "neither backend calls into
the other" posture).

* **`MediaExtensions`** (`src/application/media`) gains `.svg` in `kImageExtensions` — SVG becomes
  previewable, hover-previewable, and thumbnailable everywhere `isImageExtension` is already
  checked, with no per-call-site change — plus two new predicates, `isAnimatedImageExtension`
  (`.gif` only, for now) and `isVectorImageExtension` (`.svg` only), used by every surface below so
  "what counts as animated/vector" can't drift between them.
* **`QtSvgImageDecoder`** (new, `src/adapters/media`, implements `IMediaDecoder`) rasterizes via
  `QSvgRenderer` entirely internally — reads the file through the same `WindowsLongPath`-safe
  whole-file read `VipsImageDecoder` uses, renders into a target box that *fits inside* the
  requested max size (vector art has no native pixel ceiling to cap at, unlike
  `VipsImageDecoder`'s "never upsize" rule), and encodes to PNG (not JPEG) so alpha survives without
  needing `VipsImageDecoder`'s checkerboard-compositing trick. `MediaMetadata` is populated from the
  SVG's own intrinsic `defaultSize()`. `MediaDecoder::generateThumbnail()` checks `.svg` ahead of
  the generic `isImageExtension` branch to dispatch here. Swapping to a different SVG backend later
  means writing one more class with this same shape and changing one member declaration in
  `MediaDecoder` — nothing outside `src/adapters/media` ever touches `QSvgRenderer`.
* **Preview tab, GIF**: `PreviewPanelWidget` grows a second mode combo (`m_gifModeCombo`, "Static
  image"/"Animation", shown only for `.gif`, independent of the existing Video static/player combo)
  and a new `GifPlayerWidget` (`src/ui/widgets/preview`, new) stack page. `GifPlayerWidget` wraps a
  `QLabel`+`QMovie` with the same `load(path)`/`release()` contract `VideoPlayerWidget` already
  established, and — like `VideoPlayerWidget::load()` — feeds `QMovie::setFileName()` the real file
  path directly rather than going through `IMediaDecoder`/Ports, since Qt's own streaming classes
  manage their own incremental file I/O. Defaults to "Static image", matching the video combo's
  existing default-to-static posture; the existing static-thumbnail path (and its JPEG re-encode)
  is untouched and still what "Static image" shows.
* **Preview tab, SVG**: no SVG-specific UI code — it rides the same `MediaPreviewWidget` static-
  image path every other image format uses. The one shared fix needed:
  `MediaPreviewWidget::setPreview()`'s `QPixmap::loadFromData()` call dropped its hardcoded `"JPG"`
  format hint (now auto-detects from the byte signature) so the new PNG-encoded SVG bytes decode —
  a no-op for existing JPEG thumbnails.
* **Hover popup, GIF**: always animated, never static (unlike the Preview tab's opt-in dropdown) —
  `FileBrowserView`'s hover debounce-timer handler checks `isAnimatedImageExtension` and calls a new
  `MediaHoverPreviewPopup::showAnimatedGif(path, anchor)` directly instead of emitting
  `mediaHoverPreviewRequested`, bypassing the async thumbnail pipeline entirely for this one case.
  `MediaHoverPreviewPopup` gained a `QMovie` member (`frameChanged` rescales into the same
  `kMaxPreviewSize`-bounded `m_pixmap` the static path already draws from) and a `hideEvent()`
  override that stops it — covering every existing dismissal path (`clearHoverPreview()`'s row-
  change/Leave/mouse-press calls) with no new call sites needing to know about the movie.
* **Hover popup, SVG**: no new code — `.svg` joining `kImageExtensions` makes it satisfy
  `updateHoverPreview()`'s existing `isImageExtension || isVideoExtension` candidate check, so it
  flows through the existing decode-then-`showPreview()` path for free.
* **Not built**: animated SVG (SMIL) rendering — a single static `QSvgRenderer::render()` call per
  request, no re-render loop; GIF playback controls (play/pause/seek) — `GifPlayerWidget` just loops
  per the file's own embedded loop-count metadata; any change to video's existing Static
  image/Video player combo or pipeline.
* **Testing**: new adapter-level coverage for `QtSvgImageDecoder` (malformed SVG, missing file, a
  real small `.svg` fixture) mirroring `VipsImageDecoderTest`'s shape, and a `MediaDecoderTest` case
  asserting `.svg` dispatches ahead of the generic image branch. No new coverage for the Preview-tab/
  hover-popup widgets themselves — same UI-glue carve-out as §14.71/§14.76 above.

---
### 14.81 Bug Fix: GIF Thumbnails Never Decoded

§14.79's "existing static-thumbnail path (and its JPEG re-encode) is untouched" claim for GIF was
wrong in practice: this vcpkg build's `libvips` port (`vcpkg.json`'s `libvips` only requests the
`jpeg`/`png`/`webp` features) never enables libvips' own built-in GIF loader, so
`VipsImageDecoder::generateThumbnail()` silently failed for every `.gif` — no static thumbnail in
the Preview tab, no grid/filmstrip/details tile in Media Viewer mode (§14.62/§14.69), just the
generic placeholder icon everywhere a decoded thumbnail was expected. Adding a `gif` vcpkg feature
isn't an option — libvips' port doesn't expose one (its GIF loader is an upstream Meson
feature this port's `portfile.cmake` never requests). `GifPlayerWidget`/`MediaHoverPreviewPopup`'s
animated playback (§14.79) was unaffected, since both go through `QMovie` directly rather than
`IMediaDecoder`.

* **`QtGifImageDecoder`** (new, `src/adapters/media`, implements `IMediaDecoder`) decodes a GIF's
  first frame via `QImageReader` instead of libvips — the same Qt-native approach
  `QtSvgImageDecoder` already uses for SVG, and the exact same `QGifPlugin` Qt already ships and
  deploys next to `Exp-LORer.exe` (`src/app/CMakeLists.txt`'s existing `imageformats` deploy loop)
  for `GifPlayerWidget`'s own `QMovie` use. Reads the file through the same
  `WindowsLongPath`-safe whole-file read every other decoder uses, scales down to fit the
  requested box only if the native frame exceeds it (same "never upsize" rule as
  `VipsImageDecoder`'s `VIPS_SIZE_DOWN`), and encodes to PNG (preserving any real alpha) rather
  than `VipsImageDecoder`'s checkerboard-composite-then-JPEG approach — see §14.82 for why a
  consumer needed to change to tolerate that. `MediaDecoder::generateThumbnail()` checks
  `isAnimatedImageExtension` ahead of the generic `isImageExtension` branch (alongside the
  existing `isVectorImageExtension` check) to dispatch here instead of to `VipsImageDecoder`.
* **`tests/adapters/CMakeLists.txt`** gained its own `imageformats/qgif.dll` POST_BUILD deploy
  step for `explorer_adapters_tests.exe`, mirroring the existing `sqldrivers/qsqlite.dll` one
  right above it — without it, `QtGifImageDecoderTest`'s real-GIF case failed in exactly the way
  this bug fix exists to catch (`QImageReader::read()` silently returning a null `QImage`),
  despite `Exp-LORer.exe` itself already deploying the same plugin.
* **Not built**: fixing libvips' own GIF support (no feature exists to request); any change to
  `GifPlayerWidget`/`MediaHoverPreviewPopup`'s animated-playback path (already correct).
* **Testing**: new `QtGifImageDecoderTest` (missing file, garbage bytes, a real 1x1 GIF fixture —
  dimensions, codec, and the PNG signature) mirroring `QtSvgImageDecoderTest`'s shape, and a
  `MediaDecoderTest` case asserting `.gif` dispatches to `QtGifImageDecoder` ahead of the generic
  image branch.

---
### 14.84 Bug Fix: Transparent SVG/GIF Previews Invisible in the Hover Popup and Preview Tab

The same failure §14.82 fixed for Media Viewer thumbnails also affected the two other surfaces
that show a decoded `QtSvgImageDecoder`/`QtGifImageDecoder` image at full/near-full size with its
real alpha channel preserved: `MediaHoverPreviewPopup` (the normal browser's hover-popup) painted
its background with the popup's own `palette().window()` first, and the Preview tab's
`MediaPreviewWidget` just handed the scaled `QPixmap` straight to a plain `QLabel::setPixmap()` —
both let a transparent-but-dark SVG/GIF vanish into whatever colour sat behind it (most visibly a
dark theme's window background). Pure UI fix, no Domain/Application/Port change.

---
### 14.85 Preview Panel: Content-Only Mode Switching + Collapsible Properties

§14.67/§14.79's "Static image" ↔ "Video player"/"Animation" combos worked by swapping
`PreviewPanelWidget`'s *outer* `QStackedWidget` between `MediaPreviewWidget` (which privately
owned the name label, Properties `QGroupBox`, and zoom label alongside the static-image
`QLabel`) and a bare `VideoPlayerWidget`/`GifPlayerWidget` with none of that chrome — so flipping
either combo tore down and rebuilt the filename/Properties/zoom each time, since they only
existed on the static-image page. Separately, the Properties box was a plain, always-expanded
`QGroupBox`.

* **`MediaPreviewWidget`** gained an inner `m_contentStack` (`QStackedWidget`) holding the
  static-image `QLabel` plus `VideoPlayerWidget` and `GifPlayerWidget`, which moved in from being
  `PreviewPanelWidget`'s outer-stack siblings. The name label, Properties box, and zoom label
  stay outside `m_contentStack`, as permanent siblings in `MediaPreviewWidget`'s own layout — so
  `PreviewPanelWidget`'s outer stack now stays parked on `MediaPreviewWidget` for the whole
  Image/Video/GIF session, and only `m_contentStack` swaps on a mode-combo change.
  `setPreview()` was split into `setMetadata()` (name + Properties text, always called regardless
  of the active content sub-page — fixing a latent gap where selecting a new video/GIF while the
  combo was already on "Video player"/"Animation" never updated them, harmless only because they
  were invisible on that page before this change) and `showStaticImage()` /
  `showVideo()` / `showGifAnimation()` (each switches `m_contentStack` and returns/loads
  accordingly). The zoom label was initially hidden whenever `m_contentStack`'s current page
  wasn't the image label, since zoom had no equivalent for a native video/GIF player -- but
  switching the mode combo made it visibly vanish, which read as a glitch rather than intentional,
  so it was reworked to stay visible across all three content modes: `updateZoomLabel()` computes
  the percentage from `m_contentStack`'s own size against the actual resolution (rather than the
  static image label's rendered pixmap), matching how both the image label and `GifPlayerWidget`
  actually letterbox (`Qt::KeepAspectRatio` against their full area); for the video player it's a
  slight overestimate since `VideoPlayerWidget`'s own controls bar eats into its available height,
  accepted as preferable to the label vanishing. `rescaleImage()` keeps doing only the image
  pixmap's own rescale/checkerboard-compositing; `updateZoomLabel()` is called from `setMetadata()`
  (so switching files refreshes it immediately regardless of which mode is active) and from
  `resizeEvent()` (unconditionally, not gated on the image page). The label is hidden only when
  there is no resolution metadata at all.
  `PreviewPanelWidget`'s release call sites (`hideEvent`, `onPreviewLoading/Failed/Cleared`, the
  mode-change handlers) now forward through four small `MediaPreviewWidget` methods
  (`releaseVideoPlayer()`/`releaseGifPlayer()`, and the guarded `...IfActive()` pair) instead of
  touching the two player widgets directly.
* **Collapsible Properties**: no generic collapsible-section widget exists in this codebase
  (`SidePanelWidget`/`BottomPanelWidget`'s collapse pattern is panel/column-level, driven by an
  owning `QSplitter` — not a fit here), so `m_propertiesGroupBox` was made checkable instead
  (`setCheckable(true)`, default checked/expanded) with its native `toggled(bool)` wired straight
  to `m_propertiesLabel->setVisible(bool)` — a standard Qt checkbox-in-title-bar affordance, no
  new widget class. Collapsed/expanded state is session-only (not persisted via
  `AppConfig`/`QSettings`), the same posture §14.67/§14.79 already took for the two mode combos
  in this same widget.
* **Not built**: no change to the combos' own show/hide logic, their session-only persistence, or
  the "hide the whole Properties box when there are no properties" behavior (independent of, and
  composes fine with, the new collapse toggle).
* **Testing**: no new GTest coverage — same UI-glue carve-out as §14.71/§14.76/§14.84 above.

* **`MediaThumbnailPainting`** (`src/ui/widgets/media`) gains a public `checkerboardBrush()`,
  exposing the same static 8px gray/white tile `paintThumbnail()` already built for itself
  (§14.82) instead of duplicating the pattern a third time.
* **`MediaHoverPreviewPopup::paintEvent()`** fills the whole popup rect with that checkerboard
  instead of `palette().window()` whenever `m_pixmap.hasAlphaChannel()` — covers both
  `showPreview()`'s static pixmap and `showAnimatedGif()`'s per-frame `QMovie` pixmap for free,
  since both funnel through the same `m_pixmap` member and `paintEvent()`.
* **`MediaPreviewWidget::rescaleImage()`** composites the already-scaled pixmap onto a same-size
  checkerboard `QPixmap` (via a throwaway `QPainter`) before calling `QLabel::setPixmap()`,
  whenever the scaled pixmap's `hasAlphaChannel()` is true — composited only behind the image
  itself, not the whole label, since `m_imageLabel` aligns the pixmap top-centre within whatever
  extra space the panel gives it.
* **Not built**: `GifPlayerWidget` (the Preview tab's "Animation" mode for GIFs, a plain
  `QLabel::setMovie()`) — left unchanged; the user's report was specifically about the static
  SVG/GIF-frame surfaces, and `QLabel` has no easy per-frame paint hook to composite a checkerboard
  behind a running `QMovie` the way the two fixed surfaces could.
* **Testing**: no new GTest coverage — pure Qt painting code in the UI/ViewModel-glue layer, same
  carve-out as §14.82; manual smoke test only (hover a transparent SVG/GIF and open one in the
  Preview tab, in a dark theme, and confirm dark content is now visible against the checkerboard).

---
### 14.98 Preview Panel: Built-In Text Editor Integration

Replaces the text page's bare read-only `QPlainTextEdit` (`TextPreviewWidget`, §14.26/§14.58) with
the real editor built up over §14.93–14.97 (`src/ui/widgets/texteditor/`) — opened read-only for
plain browsing, with Edit/Revert/Save controls to flip it into a real editing session and persist
changes. §14.97's own closing note already named this as its reason for existing.

* **New read/write capability, Domain untouched.** `IFileSystemRepository` gains two methods
  alongside the existing capped `readFilePrefix` (§14.26): `readFile(path)` (full, uncapped read)
  and `writeFile(path, content)` (overwrite). `StandardFileSystemRepository` implements both by
  mirroring existing patterns one-for-one — `readFile` is `readFilePrefix` without the `maxBytes`
  cap; `writeFile` reuses `createFileFromTemplate`'s `isInsideArchive(path)` guard so saving into a
  virtual/archive-internal path fails with a reportable `Error` instead of silently writing to a
  temp copy that's never persisted (archives are read-only, §14.29).
* **`TextFileEditUseCase`** (new, `src/application/texteditor/`) — the single entry point for this
  feature's business logic, stateless and depending only on `IFileSystemRepository`, same posture
  as `FilePreviewUseCase`/`FileTransferUseCase`. `readFullText(path)` calls
  `materializeForReading(path)` first when `path` has an archive ancestor (the same
  `archiveAncestorInPath` precedent `FilePreviewUseCase::generatePreview` already follows before
  `readFilePrefix`), then `readFile` on the resolved path. `writeText(path, content)` calls
  `writeFile` directly on the **original** path, deliberately never materialized — a save must
  target the real file, never a temp-extracted archive copy.
* **`TextFileEditWorker`/`TextFileEditViewModel`** (new, `src/adapters/viewmodels/texteditor/`) —
  mirror `FilePreviewWorker`/`FilePreviewViewModel`'s shape exactly: one dedicated background
  `QThread` + always-alive worker, dispatched via `QMetaObject::invokeMethod(...,
  Qt::QueuedConnection)` rather than Qt signals/slots, since `Result<std::string>`/`Result<void>`
  aren't Qt-meta-type-registered. Unlike `FilePreviewViewModel`, there is no generation-counter
  staleness logic — Edit/Save are explicit, user-triggered, single-flight actions (the widget
  disables Edit/Save while a request is outstanding), not reactive to rapid selection changes — and
  no per-tab retargeting, since there is exactly one Preview panel/text editor in the app;
  `CompositionRoot` owns `TextFileEditUseCase`/`TextFileEditViewModel` directly (own background
  thread, started/stopped in its ctor/dtor) rather than inside `WorkspaceController` the way
  `FilePreviewViewModel` is, the same "owned long-lived by `CompositionRoot`" posture
  `FileTagsService`/`AdvancedSearchService` already use for a feature with no per-pane retargeting
  need.
* **`TextEditPreviewWidget`** (new, `src/ui/widgets/preview/`, replacing `TextPreviewWidget` in
  `PreviewPanelWidget`'s stack) — a toolbar (`Edit`/`Revert`/`Save` `QToolButton`s, plus an inline
  error label) above a `TextEditorPanel`. One instance lives for the life of the Preview panel; its
  inner `TextEditorPanel` is destroyed and rebuilt fresh every time `setPreview()` targets a new
  file — a clean slate with no leftover find-bar/cursor/undo state, and it sidesteps
  `TextEditorOptions` being construction-time-only for everything except `readOnly` (§14.97's
  runtime setter covers that one).
  * `setPreview()`: builds read-only, populated from the already-capped preview text
    (`preview.text()`/`textTruncated()`, the same source §14.26 always used) — plain browsing has
    zero behavior/performance change from before. Options come from `defaultOptions()` (below) plus
    `highlightLanguage` via the same `LanguageRegistry::languageForExtension` call
    `TextPreviewWidget::setPreview` used. The inner editor keeps the `previewTextView` objectName
    `TextPreviewWidget` used, so existing `ui_tests/` automation (`right_panel.py`'s `text_view`)
    needed no changes.
  * `Edit`: disables itself, calls `TextFileEditViewModel::requestLoad(path)`. On success, swaps in
    the full content (cached for `Revert`), flips `TextEditorPanel::setReadOnly(false)`, and connects
    `document()->modificationChanged` to drive `Save`'s enabled state live. On failure, re-enables
    `Edit` and shows the error inline.
  * `Revert`: reloads the cached last-loaded content via `setPlainText()` — discards edits but
    **stays in edit mode**; `setPlainText()` already clears the modified flag for free (no manual
    dirty-tracking needed anywhere in this stack — `QTextDocument` already owns that), which fires
    `modificationChanged(false)` and disables `Save` through the same connection `Edit` set up.
  * `Save` (click or Ctrl+S, a `QShortcut` on this widget at `Qt::WidgetWithChildrenShortcut` scope
    — same convention the Find/Go-to-Line bars use, and the app's first Ctrl+S binding): calls
    `TextFileEditViewModel::requestSave(path, toPlainText())`. On success, **stays in edit mode**
    and clears the modified flag (which disables `Save` again via the same connection). On failure,
    leaves state as-is and shows the error inline, re-enabling `Save` since the content is still
    modified.
  * **Unsaved-changes guard on navigating away**: `PreviewPanelWidget::onPreviewLoading()` — fired
    synchronously before the new target's content is fetched, and by then the pane's selection has
    already moved on, so it's the latest point the *old* editor still exists — now calls
    `TextEditPreviewWidget::prepareForTargetChange()` first. If the current editor is open for
    editing and `document()->isModified()`, a modal `QMessageBox` offers exactly **Save** and
    **Don't Save** (no Cancel — the selection has already changed by this point, so there's nothing
    to cancel back to). **Save** fires `requestSave` best-effort (the widget's editor is about to be
    torn down by the next `setPreview()` call either way, so a failure here has no UI left to report
    into — a documented limitation); **Don't Save** does nothing further. Either way, the old
    `TextEditorPanel` is torn down as part of the normal clear/rebuild flow.
* **Settings > General > Preview** (new page, `SettingsDialog`): four checkboxes — "Show line
  numbers", "Enable Find", "Enable Go to Line", "Enable multi-cursor editing" — all defaulting
  checked (matching `TextEditorOptions`' own defaults), persisted as `QSettings` keys under
  `Preview/*`, following `buildGeneralUiPage()`'s exact immediate-apply checkbox wiring. A static
  `TextEditPreviewWidget::defaultOptions()` reads these four keys fresh every time `setPreview()`
  rebuilds the inner `TextEditorPanel` — no live-reapply broadcast signal needed (unlike the
  tab-close-button setting, §14.27), since a settings change takes effect the next time any file is
  previewed. No "read-only default" or "highlight language" entries on this page — preview always
  opens read-only, and highlighting stays auto-detected per extension, both unchanged from before.
* **Wiring**: `PreviewPanelWidget` gains a `TextFileEditViewModel&` constructor parameter, forwarded
  straight into the `TextEditPreviewWidget` it now constructs in place of `TextPreviewWidget`;
  threaded from `MainWindow` (its own new `TextFileEditViewModel&` constructor parameter) up through
  `main.cpp`'s `CompositionRoot::textFileEditViewModel()`.
* **Not built**: guarding any dirty-editor loss path other than the in-Preview-panel
  file-selection switch — closing a tab, switching panes/workspaces some other way, or quitting the
  app with a dirty preview editor is unguarded in this pass. A file-size guard on `Edit` (a full
  synchronous-off-UI-thread read happens regardless of size, matching the project's existing posture
  toward documented-but-unaddressed scale limits, e.g. §14.96's rehighlight-per-edit note). Any
  encoding handling beyond UTF-8 — content is read/written as raw bytes decoded/encoded as UTF-8,
  identical to how `FilePreviewUseCase`/`TextPreviewWidget` already treated text.
* **Testing**: `TextFileEditUseCase` is GTest-covered against a mocked `IFileSystemRepository`
  (`tests/application/TextFileEditUseCaseTest.cpp`, mirroring `FilePreviewUseCaseTest.cpp`'s
  patterns), and `StandardFileSystemRepository::readFile`/`writeFile` against real temp files
  (`tests/adapters/StandardFileSystemRepositoryTest.cpp`), per §11's adapter-test convention. The
  `TextEditPreviewWidget`/Settings-page UI wiring falls under the existing UI/ViewModel-glue
  carve-out — no new GTest coverage, no UI blackbox coverage in this pass.

### 14.100 Expanded Highlighting Scope: YAML, PowerShell, Markdown (Block-Level)

Extends §14.27's language scope by three grammars, closing part of that section's original "Not
built" exclusion. Same mechanism, same per-language touch points as the original nine languages —
no `SyntaxHighlightEngine`/`HighlightTheme` architecture change, no Domain/Application/Port change.

* **Grammar sourcing** (vcpkg overlay ports, same shape as the existing nine — `vcpkg_from_github`
  pinned to a tag, compiling that tag's already-committed `src/parser.c`/`src/scanner.c` directly,
  no tree-sitter-CLI codegen step):
  * **YAML** — `tree-sitter-grammars/tree-sitter-yaml`. Its `src/` also ships
    `schema.core.c`/`schema.json.c`/`schema.legacy.c` alongside `parser.c`/`scanner.c`, unlike every
    prior port's plain `parser.c`(+`scanner.c`) pair — all three schema files get compiled into the
    static lib too.
  * **PowerShell** — `wharflab/tree-sitter-powershell`, chosen over the older
    `airbus-cert/tree-sitter-powershell` fork: a much larger fuzz/corpus test suite and the grammar
    proposed to the official tree-sitter org as PowerShell's successor grammar
    (tree-sitter/tree-sitter#5575). Flat `src/`, same shape as every other port.
  * **Markdown — block grammar only.** `tree-sitter-grammars/tree-sitter-markdown` is a monorepo of
    two sibling grammars, `tree-sitter-markdown/` (block structure) and `tree-sitter-markdown-inline/`
    (emphasis, links, inline code spans), joined upstream via a second parse pass over
    `ts_parser_set_included_ranges`-selected ranges. Only the block grammar is vendored — its
    overlay portfile fetches the whole repo as usual but configures from the
    `tree-sitter-markdown/` subdirectory rather than the repo root, the one deviation from the
    standard portfile template. Inline markdown (`**bold**`, `*italic*`, `` `code` ``,
    `[text](url)`) is **not** highlighted — see "Not built" below.
* **`src/adapters/highlighting`**: `Language` gains `Yaml`/`PowerShell`/`Markdown`;
  `LanguageRegistry::languageForExtension` maps `.yaml`/`.yml` → `Yaml`, `.ps1`/`.psm1`/`.psd1` →
  `PowerShell`, `.md` → `Markdown` (`.markdown` deliberately left unmapped); `displayName` gains
  `"YAML"`/`"PowerShell"`/`"Markdown"`. `SyntaxHighlightEngine` gains the matching
  `tree_sitter_<lang>` extern declarations and `tsLanguageFor`/`queryResourcePathFor` cases, same
  pattern as the original nine.
* **New `.scm` queries** (`resources/tree-sitter-queries/{yaml,powershell,markdown}.scm`),
  hand-authored like every existing query in this directory rather than copied verbatim from each
  grammar's own upstream query: YAML maps scalars/comments/anchors/tags/mapping keys onto
  `@string`/`@number`/`@boolean`/`@constant.builtin`/`@comment`/`@label`/`@type`/`@property`;
  PowerShell maps its keyword/operator sets plus `command_name`/`variable`/`type_spec` onto
  `@keyword`/`@operator`/`@function`/`@variable`/`@type`/`@string`/`@number`/`@comment`; Markdown
  (block-level) deliberately reuses the *existing six-category* base palette rather than the
  `@text.*`/`@punctuation.*` names tree-sitter's own convention would suggest: heading text →
  `@keyword`, ATX/Setext markers, fence delimiters, list markers, thematic breaks, and block-quote
  markers → `@comment` (dimmed, same as markdown renderers typically mute structural syntax), code
  fence bodies and link destinations → `@string`, link reference labels → `@type`, and
  `backslash_escape` → `@string.escape`. YAML's `boolean`/`constant`/`label`/`property` categories
  (and had Markdown used `text`/`punctuation` instead) aren't in `HighlightTheme::defaultColorFor`'s
  base palette, so a capture under one of those renders in the default foreground color until a
  user colors it from Settings — same pre-existing behavior CSS/JSON's own
  `@operator`/`@constant.builtin`/`@escape` captures already have. No base-palette change was made:
  adding one would have had to apply uniformly to every language already using that same capture
  name (e.g. `@property`/`@punctuation.*` appear in half the existing nine languages' own
  highlights.scm too), well beyond this pass's scope — Markdown's query was written to lean on the
  existing palette instead specifically so it highlights visibly without that change.
* **`HighlightThemeStore`**'s `Language`↔`QString` round-trip, `SettingsDialog`'s and
  `TextEditorOptionsDialog`'s `kAllLanguages` arrays (both `9` → `12`) all extended the same way the
  original nine languages are listed — no structural change to either dialog.
* **Not built**: Markdown inline emphasis/links/inline-code-span highlighting — needs the second
  (`tree-sitter-markdown-inline`) grammar plus an injection/sub-parse mechanism
  `SyntaxHighlightEngine` doesn't have (it runs exactly one parser+query per `Language`); adding
  that mechanism is real architecture work and was deliberately left out of this pass. `.markdown`
  as a second Markdown extension. Any `HighlightTheme` base-palette entries for YAML's
  `boolean`/`constant`/`label`/`property` capture categories.
* **Testing**: `tests/adapters/SyntaxHighlightEngineTest.cpp` — `MapsKnownExtensionsToTheirLanguage`
  extended for the six new/sibling extensions; the now-stale `.md`/`.yaml` cases dropped from
  `ReturnsNulloptForExtensionsOutOfScope`; one new `Highlights<Lang>KeywordAndComment`-style test per
  new language, same `hasSpanCovering` helper the existing nine languages' tests already use.

---
### 14.101 PDF Preview (Static First Page)

Extends the Preview tab (§14.26) to `.pdf` files: selecting a PDF shows its first page rendered as
a static raster image through the existing `MediaPreviewWidget`, the same composite (filename +
image + collapsible Properties + zoom label) image/video previews already use — deliberately scoped
like image/video preview's own original pass, which shipped a static poster frame with "no inline
playback" as an explicit follow-up (§14.26); page navigation for a multi-page PDF is the equivalent
follow-up here and is **not built** in this pass (see below). `.pdf` is a new, standalone extension
classification — it does **not** join `MediaExtensions::isImageExtension`, so Media Viewer mode,
Extra Large Icons (§14.71), and the hover-preview popup (§14.76) — all gated on
`isImageExtension`/`isVideoExtension` — do not pick up PDFs in this pass; only the Preview tab does.

* **`poppler` (new vcpkg dependency)**, consumed via `poppler-cpp` exactly the way `libvips`/FFmpeg
  already are (§6) — `pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)` in
  `src/adapters/media/CMakeLists.txt`, no CMake-config package exists for it. No extra vcpkg
  features requested (`poppler`'s own default features are sufficient; its `qt`/`cairo`/`glib`
  features are for consumers that want Poppler's own Qt/Cairo/GLib wrappers, not needed here since
  rendering is encoded to PNG via Qt directly, the same finishing step `QtSvgImageDecoder`/
  `QtGifImageDecoder` already use). Chosen over `libmupdf` (also available in this project's vcpkg
  registry) since `poppler-cpp`'s page-render API is simpler for this single-page-raster use case
  and its license (GPL-2.0-or-later) is no stricter than dependencies already in this project
  (`libgit2`, and `ffmpeg` depending on build config).
* **`MediaExtensions`** (`src/application/media`) gains `isPdfExtension(extension)` (`.pdf` only) —
  its own predicate, not folded into `isImageExtension`, the same "not a raster image" posture
  already taken for video.
* **`MediaMetadata`** (Domain) gains a `pageCount` field (`int pageCount() const`, default/unknown
  value `0`) — same "0 = unknown/not applicable" graceful-degradation posture already established
  for `bitRateBps` (§14.26). `create()` gains a trailing default parameter so every other caller is
  unaffected. Used only by the Pdf preview kind below; zero for Image/Video.
* **`CachingMediaDecoder`'s on-disk blob format** (`MetadataHeader`/`serialize`/`deserialize`,
  §8/§14.63) gains the new `pageCount` field **and a format-version tag** — the first time a field
  has been added to this already-shipped, already-populated on-disk cache format. Without a version
  check, an old (smaller) cached Image/Video entry would be misparsed by the grown struct (the fixed
  byte offsets `deserialize()` reads `codecLength`/the codec string/the trailing image bytes from
  would all shift), silently corrupting already-cached thumbnails rather than cleanly missing and
  regenerating. The version tag makes any pre-existing cache entry fail `deserialize()`'s existing
  "doesn't parse as a well-formed blob → treat as a cache miss, regenerate" path (same path already
  used for truncated/corrupted entries) instead of being misread as the new layout.
* **`FilePreview`** (Domain) gains `FilePreviewKind::Pdf` and a `static FilePreview pdf(...)` factory
  — identical shape to `image()`/`video()` (encoded raster bytes, path, `MediaMetadata`,
  `fileSizeBytes`). Doc comments on `path()`/`imageBytes()`/`metadata()`/`fileSizeBytes()` extended
  to mention Pdf alongside Image/Video.
* **`PopplerPdfDecoder`** (new, `src/adapters/media`, implements `IMediaDecoder`) — self-contained,
  not cross-calling any other decoder (the established posture, §14.26/§14.79). Reads the whole file
  via the same `WindowsLongPath`-safe `std::ifstream` helper every other decoder duplicates locally,
  then opens it with poppler-cpp's `document::load_from_raw_data()` (a memory-buffer API, not
  `load_from_file()`'s narrow-filename one) — never hands poppler-cpp a raw path string, the same
  non-ASCII-safety rule §6 requires. `previewIndex` is honored as a page index (clamped to
  `[0, pageCount - 1]`) purely for forward compatibility with a future page-navigation feature at
  zero extra cost — `FilePreviewUseCase` below always calls with the default (page 0) in this pass,
  so nothing yet exercises a non-zero value. Renders directly at the DPI needed to fit the requested
  `maxWidth`/`maxHeight` box (computed analytically from the page's point size, no "decode large then
  downscale" pass needed since a PDF page is vector content — same reasoning `QtSvgImageDecoder`
  already uses), via `poppler::page_renderer`, then wraps the resulting `poppler::image`'s raw buffer
  in a `QImage` and PNG-encodes it (preserves alpha, matches the SVG/GIF decoders' encode step).
  Metadata: page's rendered width/height, codec `"pdf"`, and `pageCount` from `document::pages()`.
* **`MediaDecoder::generateThumbnail()`** gains an `isPdfExtension` branch dispatching to a new
  `m_pdfDecoder` member, alongside the existing SVG/GIF/image/video branches — order doesn't matter
  against those since `.pdf` is disjoint from all of them.
* **`FilePreviewUseCase::generatePreview()`** gains a branch after the existing video branch:
  `isPdfExtension` → `m_mediaDecoder.generateThumbnail(readablePath, maxImageWidth, maxImageHeight)`
  (page 0 implicit) → `FilePreview::pdf(...)`. Materialization for an archive-nested PDF (§14.29)
  is already handled generically before this branch, same as Image/Video.
* **Preview tab UI**: no new widget. `MediaPreviewWidget::showStaticImage()`/`rescaleImage()`/
  `updateZoomLabel()` already work off raw `imageBytes()`/resolution regardless of kind, so a PDF's
  rendered page displays through the exact same path a raster image does. Two small, targeted
  changes only: `MediaPreviewWidget::setMetadata()` gains a `kind() == Pdf && pageCount() > 0` branch
  adding a "Pages: %1" row to the Properties block (parallel to Video's existing Length/Bitrate
  rows); `PreviewPanelWidget::onPreviewReady()` gains a `case FilePreviewKind::Pdf:` calling
  `setMetadata()` + `showStaticImage()` and switching to `m_mediaPreviewWidget` (falling back to the
  unsupported label on a decode failure, same as Image) — no mode combo shown, since there is no
  alternate rendering mode for Pdf in this pass.
* **Not built**: page navigation (Previous/Next through a multi-page PDF) — deferred to a later pass,
  the same way the inline video player (§14.67) followed well after the original static poster-frame
  pass; `previewIndex` is wired through the decoder for exactly this reason, so that follow-up needs
  no adapter/use-case rework, only a `FilePreviewViewModel`/`FilePreviewWorker` "current page" request
  path and Preview-tab UI. Also not built: password-protected PDF handling (fails like any other
  decode error); text extraction/selection/search within the preview; Extra Large Icons, Media Viewer
  grid, and hover-popup support (all gated on `isImageExtension`/`isVideoExtension`, which `.pdf`
  deliberately doesn't join); thumbnailing any page beyond the first.
* **Testing**: `MediaMetadataTest` gains `pageCount` default/round-trip coverage;
  `CachingMediaDecoderTest` gains a case asserting a stale-format cached blob is treated as a miss
  (not misparsed); `PopplerPdfDecoderTest` (new, mirrors `QtSvgImageDecoderTest`'s shape — missing
  file, garbage bytes, and a minimal hand-written single-page PDF literal embedded in the test, the
  same "write the fixture content directly in the test, no binary fixture file" convention
  `QtSvgImageDecoderTest`/`QtGifImageDecoderTest` already use) covers page count, rendered
  dimensions, and the PNG signature; `MediaDecoderTest` gains a `.pdf`-dispatches-to-PopplerPdfDecoder
  case; `FilePreviewUseCaseTest`/`tests/domain/FilePreviewTest.cpp` gain Pdf-kind coverage mirroring
  their existing Image/Video cases. UI changes fall under the existing UI/ViewModel-glue carve-out
  (§11) — no new GTest coverage there, consistent with every other Preview-tab UI change above.

### 14.106 Preview Tab: Extensionless Config Dotfiles Shown as Text

Extensionless config dotfiles (`.gitignore`, `.gitattributes`, `.gitmodules`, `.gitconfig`,
`.mailmap`, `.editorconfig`, `.clang-format`, `.clang-tidy`, `.dockerignore`, `.npmignore`,
`.npmrc`) now preview as text instead of falling through to Unsupported.
`FilePreviewUseCase::generatePreview()`'s existing text branch gains a second, filename-based
entry condition — a new `isTextDotfile()` helper alongside `isTextExtension()` — checked only as
a fallback when the extension-based lookup finds no match, so the curated list stays exact rather
than treating every dotfile as text. The lookup matches on the target's own (possibly virtual,
archive-nested per §14.29) path's filename, lowercased via `PathUtf8::toUtf8()` +
ASCII `std::tolower` (these names are all-ASCII, so no `UnicodeCaseFold` is needed), so an
archive-nested `.gitignore` matches the same way a top-level one does. No syntax highlighting is
added or needed: `LanguageRegistry::languageForExtension()` keys on `path.extension()`, which is
already empty for a lone leading-dot filename, so these fall back to plain text automatically.

