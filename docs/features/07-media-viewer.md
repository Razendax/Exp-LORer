# Architecture Document — Feature Decision Log: Media Viewer

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.61–14.66, §14.68–14.69, §14.72–14.78, §14.80, §14.82 — the recursive tag/name-filtered image/video gallery mode and
its follow-ups: thumbnail previews/metadata overlays, the `IMediaDecoder` thumbnail/metadata
merge, video hover-scrub previews, loading-progress notification, kind-scoped loading,
sort/view/filmstrip/details controls, the Filter/Apply tag-click behaviour toggle,
folder-grouped browsing, inline rename support, thumbnail-decode performance work (batched
multi-frame video decode, a parallel decode pool), and a transparency-background bug fix shared
by every thumbnail tile. The built-in video player itself
(§14.67) lives in [05-preview-and-decoration.md](05-preview-and-decoration.md), since it was built
for and first used by the Preview tab. Section numbers are **not** contiguous with this file's
neighbors — they were assigned chronologically as each feature was built and are kept stable
since code comments cite them directly. See [Architecture.md](../Architecture.md) §14 for the
full topic index across all nine files.

---
### 14.61 Media Viewer Mode

A new per-tab mode (View menu, "Media Viewer Mode," checkable) that replaces the pane's normal
browsing with a full-grid gallery of every image or video found recursively under whatever folder
the tab was showing when the mode was entered, filterable by AND-combined tags and a name/toggle
filter from a new right-panel "Searcher" tab. Entirely UI/Adapters — reuses `AdvancedSearchService`'s
existing recursive-scan/tag-first-candidate requests, `TagManagementUseCase::findFilesWithAllTags`
(§14.20's "tag-first" design), and `MediaExtensions::isImageExtension`/`isVideoExtension`; no new
Port or Domain/Application change.

* **`MediaViewerKind`** (`src/adapters/viewmodels/media/MediaViewerKind.h`, new): `enum class
  MediaViewerKind { Image, Video }`, same minimal posture as `ViewMode.h`.
* **`TabViewModel`** gains a third per-tab `FileListModel* m_mediaViewerResultsModel`, alongside
  `m_fileListModel`/`m_advancedSearchResultsModel`, plus `mediaViewerActive()`/`enterMediaViewer()`/
  `exitMediaViewer()`/`setMediaViewerKind()`/`addMediaViewerTagFilter()`/
  `removeMediaViewerTagFilter()`/`setMediaViewerNameFilter()` and matching
  `mediaViewerModeChanged`/`mediaViewerResultsChanged`/`mediaViewerKindChanged`/
  `mediaViewerCriteriaChanged`/`mediaViewerLoading(Finished)`/`mediaViewerFailed` signals.
  `enterMediaViewer()` captures `currentPath()` as the fixed recursive root, resets kind to `Image`
  and all filters, exits quick/advanced search if active (single-active-mode invariant, same as
  `showAdvancedSearchPanel`), and dispatches the initial candidate fetch. The candidate list (tag-first
  via `findFilesWithAllTags` when tags are applied, otherwise a full `listDirectoryRecursive` scan,
  both through the already-injected `AdvancedSearchService&`) is cached and **only re-fetched when the
  applied tag set changes** — the Images/Videos dropdown and the name/toggle filter re-filter that
  cached list in memory (`MediaExtensions`-based kind filter, then `FileNavigationUseCase::
  filterByName`), mirroring `runAdvancedSearch`'s cached-snapshot split. Deliberately **does not**
  port Advanced Search's partial-snapshot/progress-tick machinery (§14.37/§14.38) — only a single
  `m_mediaViewerGeneration` counter guards against a stale async reply — since this is a narrower,
  usually-fast, single-root query; a plain busy signal pair covers the UI instead of incremental
  results.
* **`FileBrowserView`** gains `DisplayMode::MediaViewerGrid`, alongside the existing
  `AdvancedSearchResults` (§14.19) — same "ignore `setViewMode()`, lock the rendering" shape, but
  locked to the icon-grid (`m_iconDelegate`, `IconMode`, `ExtraLargeIcons` sizing) instead of the
  Details tree. Rename (F2/context menu) stays disabled in this mode too, same guard as
  `AdvancedSearchResults`.
* **`WorkspacePaneWidget::addPageForTab`** adds a third per-tab stack page: a `FileBrowserView` bound
  to `tab->mediaViewerResultsModel()` in `MediaViewerGrid` mode, wired through the same
  `wireBrowserView()` every other page uses (selection/activation/context-menu all come for free, same
  rationale as §14.18). `pickStackPage` now picks page 2 when `mediaViewerActive()`, ahead of page 1's
  `advancedSearchActive()` check. The same signal also hides the tab's `BottomPanelWidget` and disables
  the pane's back/forward/up/refresh/address-bar/view-mode toolbar for the duration — the grid's root
  is fixed at entry, so navigation is locked rather than attempting to re-scope it live; exiting (the
  Searcher tab's "Exit viewer mode" button, or unchecking the View-menu action) returns to normal
  browsing at the same folder.
* **`SidePanelWidget`** gains one new method, `setPanelTabButtonVisible(int, bool)`, so a panel tab's
  header button can be hidden/shown at runtime without touching `addPanelTab`/`activateTab`. The new
  right-panel "Searcher" tab is registered at startup like Tags/Preview/Monitoring but starts hidden,
  only appearing (with Tags/Preview/Monitoring hidden in turn) while any tab has Media Viewer mode
  active, restoring on exit. The left panel is hidden outright (`setVisible(false)`, it's a direct
  `mainSplitter` child, same posture `WorkspaceLayoutWidget` already uses to hide inactive panes).
* **`MediaViewerSearchPanelWidget`** (`src/ui/widgets/media/`, new) is the Searcher tab's content, top
  to bottom: an Images/Videos `QComboBox`, an "Exit viewer mode" button, a `TagChipWidget::
  Kind::Removable` applied-filter row, a tag-search box + `Kind::ReadOnly` matching-tag row (wired to
  `clicked`, exactly `SearchCriteriaPanel`'s `m_tagMatchesLayout` pattern — no "+" button), and the
  same Match Case/Whole Word/Exact Name/Regex/Skip-Git-Ignored 5-button row plus a name field,
  copied from `SearchCriteriaPanel` rather than extracted into a shared widget (this codebase already
  duplicates that row between `SearchPanelWidget` and `SearchCriteriaPanel`, so a third copy follows
  existing precedent rather than introducing a new abstraction).
* **`MainWindow`**: a new checkable View-menu action drives `enterMediaViewer()`/`exitMediaViewer()`
  on `m_workspaceController->focusedTab()`. `createWorkspace()` registers the Searcher tab as the right
  panel's 4th tab (`kRightPanelTabCount` → 4), hidden immediately after registration. `
  onFocusedTabChanged` gets a new `m_mediaViewerTrackedTab` disconnect/connect block — directly
  mirroring the existing `m_searchTrackedTab`/`m_searchPanelWidget` block rather than introducing a
  dedicated ViewModel class — wiring the focused tab's media-viewer signals to
  `m_mediaViewerSearchPanelWidget`'s setters (and the widget's own signals back to that tab), plus
  hiding/showing the left panel and swapping the right panel's visible tab on
  `mediaViewerModeChanged`. The persisted right-panel active-tab-index restore at startup is clamped
  to exclude the Searcher slot, since the mode is never persisted (same posture as search/advanced
  search — only path/ViewMode/sort/active-tab-index survive in `TabConfig`).
* **Not built:** live re-scoping of the grid if the user could somehow navigate mid-session (they
  can't — navigation is locked, see above); persisting Media Viewer mode, the kind selection, or its
  filters across restarts; Advanced Search's progress-tick partial-result UI (an explicit v1
  simplification, see above) — can be added later if a very large recursive scan proves slow enough
  to need it.
* **Testing**: no new GTest coverage — `TabViewModel`/`AdvancedSearchService`/`SearchCriteria` have no
  existing tests either; this layer's Qt-coupled ViewModel orchestration stays under the existing §11
  UI/ViewModel-glue carve-out, verified by manual smoke test.

### 14.62 Media Viewer Mode: Thumbnail Previews & Metadata Overlays

§14.61's grid originally rendered every row through the plain `FileIconDelegate`/`resolveIcon()`
path used everywhere else (the generic Explorer file-type icon via `IconLoadingService`, never a
decoded preview of the image/video's actual pixels). This wires in real decoded thumbnails plus two
small corner metadata labels per tile, reusing the decode/cache plumbing `ThumbnailCache`'s own
header comment says it was "written so the future grid-icon thumbnail feature can reuse it
unchanged" — the same `IMediaDecoder::generateThumbnail`/`extractMetadata` via
`MediaProcessingUseCase` over the already-shared `CachingMediaDecoder`/`ThumbnailCache`
(`CompositionRoot.h`) the Preview panel uses. Entirely Adapters/UI — no new Port, no Domain/
Application/schema/layering change.

* **`MediaThumbnailData`** (`src/adapters/viewmodels/media/MediaThumbnailData.h`, new): `{ QImage
  image; int width = 0; int height = 0; std::chrono::milliseconds duration{}; }`,
  `Q_DECLARE_METATYPE`'d so it can travel through `QVariant` the way `Tag`/`std::vector<Tag>` do.
* **`MediaThumbnailLoadingWorker`/`MediaThumbnailLoadingService`** (`src/adapters/viewmodels/media/`,
  new) mirror `IconLoadingWorker`/`IconLoadingService`'s chunked-request/per-context-staleness shape,
  but with one dedicated `QThread` rather than a pool — the same single-serialized-worker shape every
  other background `*Service` in this codebase uses (`GitStatusService`/`DiskUsageService`/
  `FragmentationService`/`FilePreviewWorker`). `IconLoadingService`'s pool is explicitly justified by
  cheap-but-many synchronous shell/GDI calls that parallelize well; thumbnail decode instead goes
  through the one shared `MediaProcessingUseCase`/`CachingMediaDecoder` instance, so a pool would just
  contend for the same decoder/cache rather than parallelizing real work. Chunks are ~10 entries
  (smaller than `IconLoadingService`'s 20, since decode is heavier per item). Per item:
  `MediaProcessingUseCase::generateThumbnail()` → `QImage::fromData()` (fine off the UI thread — no
  GUI-thread affinity, same as `IconLoadingWorker`'s `QImage` results) + `extractMetadata()`, packaged
  into a `MediaThumbnailData`. `CompositionRoot` gains `MediaProcessingUseCase
  m_mediaProcessingUseCase` (constructed over the existing `m_cachingMediaDecoder`, same posture as
  `m_filePreviewUseCase`) and `MediaThumbnailLoadingService m_mediaThumbnailLoadingService`, plus a
  `mediaThumbnailLoadingService()` accessor threaded through `createTabViewModel()`/
  `createWorkspacePaneViewModel()`/`createWorkspaceController()` and down through
  `WorkspacePaneViewModel`/`WorkspaceController` into `TabViewModel`'s constructor, alongside the
  existing `IconLoadingService&`.
* **`FileListModel`** gains a `MediaThumbnailRole` (`NameColumn` only) returning a
  `MediaThumbnailData` `QVariant` once decoded, a `m_mediaThumbnailsByPath` cache (cleared in
  `setEntries()` alongside `m_iconCache`), and a `setMediaThumbnails(const QHash<QString,
  MediaThumbnailData>&)` slot that merges a batch in and emits a scoped
  `dataChanged({MediaThumbnailRole})` — identical shape to the existing `setIcons()`. The already-
  resolved generic icon (§14.61's original behavior) keeps showing as the placeholder until each
  row's real thumbnail batch arrives, same "placeholder now, real data later" relationship
  `setIcons()` already has with `resolveIcon()`'s cache-miss fallback.
* **`TabViewModel`** gains `requestMediaViewerResultThumbnails(generation, candidates)`, called from
  `onMediaViewerCandidatesReady` right alongside the existing `requestMediaViewerResultIcons` call —
  same generation-guard discard pattern, same `256×256` max size `FileBrowserView.cpp`'s
  `iconSizeFor(ViewMode::ExtraLargeIcons)` already uses for this grid (the UI-layer constant can't be
  referenced directly from the adapters layer, so it's duplicated as a literal with a comment).
* **`MediaGridTileDelegate`** (`src/ui/widgets/media/`, new) replaces `FileIconDelegate` as the
  `MediaViewerGrid` mode's installed delegate in `FileBrowserView`'s constructor
  (`DisplayMode::MediaViewerGrid` branch). Plain `QStyledItemDelegate` — no rename-edit support
  needed, since F2/rename is already disabled in this mode (so no reason to inherit
  `FileNameEditDelegate` the way `FileIconDelegate` does). `paint()` draws `MediaThumbnailRole`'s
  `QImage` aspect-fit/letterboxed when present, otherwise falls back to painting
  `Qt::DecorationRole`'s placeholder `QIcon` the same way `FileIconDelegate::paintIcon()` does; the
  filename caption and dashed selection-rect outline reuse `FileIconDelegate`'s layout math
  (`nameRectFor`/`sizeHint`), copied rather than inherited. Once a thumbnail has arrived, two small
  semi-transparent rounded-rect label chips are drawn anchored inside the thumbnail box's
  bottom-left/bottom-right corners: bottom-left is always `FileSizeFormat::format()`'s size string;
  bottom-right is `"{width}×{height}"` for an image (`MediaExtensions::isImageExtension()`) or the
  mm:ss/hh:mm:ss duration string (copied from `MediaPreviewWidget.cpp`'s formatting) for a video. The
  filename caption stays below the thumbnail, consistent with every other icon/tile delegate — the
  only visual difference between an image and a video tile is the corner label itself, no extra
  play-button glyph.
* **Not built:** any change to normal browsing's or search results' icon/thumbnail rendering;
  animated/scrubbable video preview (poster frame only, matching the existing Preview panel); live
  re-decoding on resize beyond Qt's normal pixmap scaling; persisting any new state across restarts.
* **Testing**: no new GTest coverage, matching §14.61's own precedent for this layer — this layer's
  Qt-coupled ViewModel orchestration stays under the existing §11 UI/ViewModel-glue carve-out;
  `MediaThumbnailLoadingWorker`'s per-item call is a thin pass-through to the already-tested
  `MediaProcessingUseCase`, same posture as `IconLoadingWorker` having no dedicated test.

### 14.63 IMediaDecoder: Merge generateThumbnail/extractMetadata (Performance)

Every known caller of `IMediaDecoder` (`FilePreviewUseCase`, §14.26/§14.62's
`MediaThumbnailLoadingWorker`) always called `generateThumbnail()` and `extractMetadata()` back to
back on the same file. Since the two were separate Port methods, this meant decoding/probing the
source file twice per request: `FFmpegMediaDecoder::extractMetadata()` re-ran a full
`avformat_open_input`/`avformat_find_stream_info` container probe that `generateThumbnail()` had
just done for the frame grab, and `VipsImageDecoder::extractMetadata()` re-read the whole file from
disk a second time. `CachingMediaDecoder` made it worse for images/video alike: it cached
`generateThumbnail()`'s bytes but passed `extractMetadata()` straight through uncached, so even a
thumbnail cache *hit* still paid the full metadata re-decode cost on every call.

* **`MediaThumbnail`** (`src/domain/MediaThumbnail.h`, new): a plain `{ bytes; metadata; }`
  aggregate — `IMediaDecoder`'s one remaining method, `generateThumbnail(path, maxWidth,
  maxHeight) -> Result<MediaThumbnail>`, returns both from the same decode pass.
  `extractMetadata()` is removed from the Port entirely (no caller ever needed metadata without a
  thumbnail).
* **`VipsImageDecoder`**: reads the file once; a cheap `vips_image_new_from_buffer` header-level
  open (libvips decodes pixel data lazily, so this doesn't force a full decode) yields the source
  image's own width/height for `MediaMetadata`, then the same byte buffer feeds
  `vips_thumbnail_buffer` for the actual thumbnail — one file read instead of two.
* **`FFmpegMediaDecoder`**: builds `MediaMetadata` from the same already-open
  `AVFormatContext`/`AVCodecParameters` used for the frame grab, instead of a second
  `avformat_open_input`/`avformat_find_stream_info` probe of the same file.
* **`CachingMediaDecoder`**: the cached blob now holds both the thumbnail bytes and a serialized
  `MediaMetadata` (a small fixed header + codec string + the bytes, (de)serialized entirely inside
  `CachingMediaDecoder.cpp` — `ThumbnailCache` itself stays a plain byte-blob cache, §8). A cache
  hit now returns metadata too without touching the inner decoder at all; an unparsable/corrupt
  blob is treated as a cache miss and regenerated rather than failing the request.
* Both adapters keep the existing graceful-degradation posture: if the metadata half can't be
  built (e.g. `MediaMetadata::create()`'s width/height>0 validation fails), the call still succeeds
  with zero-valued metadata rather than failing the whole thumbnail.
* **Not built**: a metadata-only Port method (no caller needs one); cross-version on-disk cache
  format compatibility (an unparsable old-format entry is just discarded and regenerated, same as
  any other corrupt entry).
* **Testing**: `VipsImageDecoderTest`/`FFmpegMediaDecoderTest` lost their now-redundant
  `ExtractMetadata*` cases (merged into the existing `GenerateThumbnail*` error-path cases);
  `CachingMediaDecoderTest` gained a case asserting a cache hit returns the originally-decoded
  metadata without a second call into the inner mock; `MediaProcessingUseCaseTest`/
  `FilePreviewUseCaseTest` updated to the merged `MockMediaDecoder` signature.

### 14.64 Media Viewer Mode: Video Hover-Scrub Previews

§14.62's video tiles show one fixed poster frame (~10% into the stream). This adds 5 equally-
spaced preview frames per video and lets the cursor's horizontal position over a tile's thumbnail
pick which one shows — the thumbnail's width is divided into 5 equal bands, band *i* (left to
right, 0-based) shows the frame at `(i + 0.5) / 5` of the stream's duration (10%/30%/50%/70%/90%);
band 0 is exactly today's existing poster-frame position. Images are unaffected: still one static
thumbnail, no hover behavior. Entirely Adapters/UI, same posture as §14.61–§14.63 — no new Port,
no Domain/Application redesign.

* **`MediaPreviewFrames.h`** (`src/application/media`, new, pure): `kCount = 5` plus
  `fractionForIndex(int)`, shared by the FFmpeg seek below, the worker's per-video decode loop, and
  the UI hover-band math, so the three can't drift apart.
* **`IMediaDecoder::generateThumbnail`/`MediaProcessingUseCase::generateThumbnail`** gain a last
  `int previewIndex = 0` parameter (ignored by image decoders; selects which of `kCount` frames a
  video decoder grabs). Defaulted so every existing call site that only ever wanted one frame
  (`FilePreviewUseCase`'s Preview-panel calls) compiles and behaves unchanged — `previewIndex = 0`
  reproduces the exact ~10% position §14.25 already established. `FFmpegMediaDecoder` replaces its
  hardcoded `duration / 10` seek with `duration * fractionForIndex(previewIndex)`;
  `VipsImageDecoder` accepts and ignores the parameter; the `MediaDecoder` dispatch facade passes
  it through.
* **`ThumbnailCache::Key`** gains `int previewIndex = 0`, folded into the existing XXH64 filename
  hash — each of a video's 5 frames becomes its own cache entry, at the same
  path+size+mtime+dimensions otherwise. `CachingMediaDecoder` threads it through to the `Key` and
  the inner decode call on a miss.
* **Cache size, widened for the new per-video 5x entry count** (images are unaffected — still one
  entry each): `ThumbnailCache`'s default on-disk cap, 512MB → 1536MB; its in-memory LRU
  (`kMemoryLruCapacity`), 20 → 60 entries, so scrubbing back and forth across several recently-
  hovered videos stays a memory hit instead of thrashing back out to disk.
* **`MediaThumbnailData`** (`src/adapters/viewmodels/media`) gains `std::vector<QImage>
  previewFrames` — empty for an image row (`image` stays the only frame, unchanged); `kCount`
  entries in time order for a video row (`previewFrames[0] == image`, decoded once and reused, not
  duplicated). **`MediaThumbnailLoadingWorker::thumbnailsFor`** branches on
  `MediaExtensions::isVideoExtension`: an image entry keeps its single existing decode call; a
  video entry makes `kCount` calls (`previewIndex` 0..4), backfilling any one failed slot from its
  nearest successfully-decoded neighbor so scrubbing never hits a blank frame, and omitting the
  entry entirely only if every slot fails (same "placeholder keeps showing" posture as today).
* **`MediaGridTileDelegate`** gains hover state (`QPersistentModelIndex` + the hovered band index,
  set/read via `setHoveredPreview`/`hoveredIndex`/`hoveredPreviewIndex`) and paints
  `previewFrames[hoveredPreviewIndex]` instead of `image` only for the currently-hovered video row
  with a complete frame set — every other row (image, non-hovered, still-decoding) paints exactly
  as §14.62 already does. `thumbnailRectFor(option)` is refactored into a cell-rect-only overload
  (`thumbnailRectForCellRect`) so `FileBrowserView` can compute the same thumbnail sub-rect without
  building a full `QStyleOptionViewItem`.
* **`FileBrowserView`** enables `setMouseTracking(true)` on the grid's `QListView` viewport only in
  `DisplayMode::MediaViewerGrid`, and extends its existing viewport `eventFilter()` with a
  `MouseMove`/`Leave` branch (observes, doesn't consume) that resolves the index under the cursor,
  skips anything that isn't a video row (`FilePathRole` + `MediaExtensions::isVideoExtension`, same
  check the delegate itself uses), maps the cursor's `x` within
  `thumbnailRectForCellRect(visualRect(index), iconSize())` to a 0-based band, and — only on an
  actual change — updates the delegate's hover state and issues a targeted `update()` on the
  previous and new hovered cells rather than the whole viewport.
* **Not built:** lazy/on-demand decoding of the 4 non-default frames (all 5 are decoded upfront
  during the existing chunked grid-population pass, same trigger point as today's single poster
  frame) — a video-heavy folder's grid now does ~5x the FFmpeg decode work to fully populate
  (images are unaffected); left as a future optimization (e.g. decode frame 0 eagerly, defer 1–4 to
  a lower-priority pass) if this proves too slow in practice. No change to the Preview panel's
  single-frame video preview, and no persistence of hover state across selection/navigation (purely
  ephemeral, cleared on mouse leave).
* **Testing**: `MockMediaDecoder`/`MediaProcessingUseCaseTest`/`FilePreviewUseCaseTest` updated to
  the new 4-argument `generateThumbnail` signature, plus a case asserting an out-of-range
  `previewIndex` fails `InvalidArgument`; `FFmpegMediaDecoderTest` gains a case covering multiple
  `previewIndex` values; `CachingMediaDecoderTest`/`ThumbnailCacheTest` gain cases asserting two
  different `previewIndex` values for the same file produce two independently cacheable entries.
  No new coverage for the `MediaThumbnailLoadingWorker`/`MediaGridTileDelegate`/`FileBrowserView`
  hover wiring, matching §14.61/§14.62's own precedent for this layer (no existing tests either) —
  manual smoke test only.

### 14.65 Media Viewer Mode: Loading Progress Notification

§14.61's "Not built" bullet flagged this gap directly: entering Media Viewer mode runs a recursive
scan (§14.38) followed by chunked thumbnail decoding (§14.62, ×5 per video since §14.64) with no
feedback beyond a static "Loading…" label bound only to the scan phase — the slower decode phase
that follows gives no indication at all. This wires entering Media Viewer mode into the same generic
progress-bar notification mechanism §14.47 built for copy/move
(`NotificationCenter::createProgressNotification`/`updateProgressBar`/`terminateProgressNotification`,
`src/adapters/notifications/NotificationCenter.h`) instead of inventing a second one. Entirely
Adapters/UI — no new Port, no Domain/Application/schema change, no change to `NotificationCenter`/
`NotificationOverlayWidget`/`ProgressNotificationWidget` itself (the existing single-bar/cancellable/
three-text-row API already covers it).

* **`TabViewModel`** is the owner/driver, exactly like `FileOperationsController` owns copy's progress
  id — it already owns the per-tab `m_mediaViewerGeneration` counter and both result-fetching call
  sites. Gains a `NotificationCenter&` constructor dependency (threaded through
  `WorkspacePaneViewModel`'s constructor and `CompositionRoot`'s `createTabViewModel()`/
  `createWorkspacePaneViewModel()` factories, alongside `WorkspaceController`'s existing
  `m_notificationCenter`) plus a `NotificationCenter::ProgressId m_mediaViewerProgressId` (0 = none
  active) and running `m_mediaViewerThumbnailsTotal`/`m_mediaViewerThumbnailsDone` counters.
* **Two phases**, matching copy's own indeterminate-then-determinate shape: (1) *Scanning* —
  indeterminate (0/0) while `AdvancedSearchService::requestRecursiveScan`/`requestTagFirstCandidates`
  runs; the recursive-scan path now passes the `onProgress` callback `requestRecursiveScan` already
  supported but `runMediaViewerQuery()` left unused, updating the bar's text with a running
  files-found count (the tag-first path has no equivalent hook, and keeps static text — its DB-driven
  lookup is comparatively fast). (2) *Loading thumbnails* — determinate `0..N`,
  `N = m_mediaViewerCandidates.size()` (the same full, kind-unfiltered set
  `requestMediaViewerResultThumbnails` already fetches); each `onBatch` chunk from
  `MediaThumbnailLoadingService` advances the bar by `batch.size()`, terminating the notification once
  `done == total`. Only the initial fresh-candidate load drives this — `applyMediaViewerFilter`'s
  re-filter calls (kind toggle/name-query keystrokes) reuse the same generation but find
  `m_mediaViewerProgressId` already back to `0` by then, so they never resurrect the notification for
  thumbnails that are already cached.
* **Cancellable** (matching copy): `NotificationCenter::progressCancelRequested(id)` is wired, when
  `id` matches, to call `exitMediaViewer()`. Same "discard, not abort" posture Architecture.md §5
  already documents for every background worker in this codebase except `FileTransferService` — no
  mid-flight interrupt of in-flight decode chunks; `exitMediaViewer()`'s existing generation bump just
  makes already-in-flight batches land and get silently discarded, same as manually toggling Media
  Viewer mode off mid-load already did before this feature.
* **Not built**: no progress reporting for `requestMediaViewerResultIcons` (generic shell icons via
  `IconLoadingService` — fast enough today not to need it); no true abort of in-flight FFmpeg decodes
  on cancel (see above).
* **Testing**: no new GTest coverage — `TabViewModel` has no existing tests either (§11 UI/ViewModel-
  glue carve-out, same precedent as §14.61/§14.62/§14.64); manual smoke test only.

### 14.66 Media Viewer Mode: Kind-Scoped Loading + Filter-Priority Decoding

§14.62/§14.65 had Media Viewer mode decode thumbnails for **both** Images and Videos the moment it's
entered, even though the Images/Videos dropdown only ever shows one kind at a time — wasting decode
time (FFmpeg video decode is the expensive case, ×5 frames/video since §14.64) on a kind the user may
never look at, and giving no priority to whatever the current name filter actually matches over a
large background sweep already mid-flight. Three fixes, entirely Adapters/UI — no new Port, no
Domain/Application/schema change.

* **Only the selected kind is decoded eagerly.** `TabViewModel::onMediaViewerCandidatesReady` no
  longer computes a both-kinds `mediaCandidates` set; it calls a new `requestKindSweep(generation,
  kind)` helper for just `m_mediaViewerKind` instead. `requestKindSweep` filters
  `m_mediaViewerCandidates` by `kind` alone (ignoring the name filter, so clearing it later never
  waits on a fresh decode pass), sizes/owns the "Loading Media Viewer" progress notification exactly
  as §14.65 did (just for one kind's count instead of both), and dispatches
  `requestMediaViewerResultIcons`/`requestMediaViewerResultThumbnails` for that kind-filtered set —
  also fixing `requestMediaViewerResultIcons`'s previous "every file in the tree" call site, since
  icons for non-media files were never shown in the grid anyway.
* **Switching kind triggers a real load.** Two per-generation bools,
  `m_mediaViewerImageSweepRequested`/`m_mediaViewerVideoSweepRequested` (reset in
  `onMediaViewerCandidatesReady` whenever `m_mediaViewerCandidates` is replaced by a fresh fetch),
  track which kind(s) have already had `requestKindSweep` dispatched this generation.
  `setMediaViewerKind` calls `requestKindSweep(m_mediaViewerGeneration, kind)` right after its
  existing `applyMediaViewerFilter(false)` re-filter; `requestKindSweep` no-ops if that kind's sweep
  already ran, so switching to a never-seen kind now visibly decodes it (and shows the progress bar
  for that switch, reusing the generic notification in place if one is already open, or opening a
  fresh one if the previous kind's sweep had already terminated it).
* **Filter-matching items jump the queue.** `MediaThumbnailLoadingService::requestThumbnails` gains a
  `Priority` parameter (`High`/`Low`), replacing its old flat "post every chunk via
  `QMetaObject::invokeMethod` immediately" dispatch with an internal mutex-guarded
  `std::deque<PendingChunk>`, pumped one chunk at a time by the same single worker `QThread` (no
  pool, same reasoning as before — everything shares one `MediaProcessingUseCase`/
  `CachingMediaDecoder` instance). A `High` dispatch first purges any of that same context's
  still-queued (not yet started) `High` chunks (a superseded filter value, e.g. the previous
  keystroke's matches), then pushes its new chunks to the deque's front; `Low` chunks are always
  left alone and simply appended to the back. The per-context "active root" staleness check moved
  from enqueue time to pop time (checked right before a chunk is actually handed to the worker).
  `TabViewModel::applyMediaViewerFilter` dispatches its kind+name-filtered subset at `High` (it's on
  screen right now); `requestKindSweep`'s whole-kind background sweep dispatches at `Low`.
  `MediaThumbnailLoadingWorker` itself is unchanged — only the service's dispatch/queueing wrapper
  changed.
* **Progress-counting correctness**: since a `High` and a `Low` dispatch can now legitimately
  overlap (a filter-matching item is both in the priority subset and part of the kind's full sweep),
  raw batch-size summation would double-count it against `m_mediaViewerThumbnailsTotal`. Replaced
  with a de-duplicated `QSet<QString> m_mediaViewerCountedPaths` (reset alongside
  `m_mediaViewerThumbnailsTotal`/`Done` whenever a sweep establishes a new total): each `onBatch`
  only advances `done` for a path not already in that set. `requestMediaViewerResultThumbnails` lost
  its original `reportProgress` bool, which gated counting to `requestKindSweep`'s own `Low` dispatch
  only — with no active name filter, `applyMediaViewerFilter`'s `High` dispatch covers most or all of
  the kind and, being the one that jumps the queue, is also the one that actually finishes decoding
  first, so the bar sat stuck at 0 (visibly so on a large folder) while the grid was already filling
  in, only catching up once the `Low` sweep's own redundant (cache-hit) decodes of the same paths
  landed behind it. Every delivered batch now counts whenever `m_mediaViewerProgressId != 0`,
  regardless of which dispatch delivered it; the path dedup already guarantees neither is counted
  twice.
* **Not built**: no change to `IconLoadingService` (icons are cheap/fast shell lookups, not the
  bottleneck this is about); no true cancellation of an in-flight FFmpeg decode already running when
  superseded — same "discard, not abort" posture §14.65 established, only **not-yet-started** chunks
  are purged/reordered; no debounce on name-filter keystrokes (purging is a cheap deque scan, not
  decode work); no persistence of sweep-requested state across restarts or re-entering Media Viewer
  mode (reset every fresh candidate fetch, same as every other Media Viewer transient state).
* **Testing**: no new GTest coverage, matching every existing Media Viewer entry in this decision log
  (§14.61/§14.62/§14.64/§14.65 all note this layer stays under the §11 UI/ViewModel-glue carve-out);
  manual smoke test only.

### 14.68 Media Viewer Mode: Sort + View Controls

Media Viewer mode's grid (§14.61) had no sort control at all -- `TabViewModel::applyMediaViewerFilter`
hard-coded `FileNavigationUseCase::sortBy(filtered, SortCriterion::Name, true)` -- and no "view"
concept beyond the single locked `FileBrowserView::DisplayMode::MediaViewerGrid`. This adds three
dropdowns (sort-by, sort-direction, view), grouped in one container widget, to the per-pane
navigation toolbar (`WorkspacePaneWidget`'s `QToolBar`) -- the toolbar that §14.61 disables
entirely while Media Viewer mode is active. The new container sits right after the address bar
and is the one piece of that toolbar that stays visible/enabled while every other control (back/
forward/up/refresh/address bar/view-mode button) keeps being disabled exactly as before.

* **New enums** (`src/adapters/viewmodels/media/MediaViewerSortCriterion.h`,
  `MediaViewerViewMode.h`, same Qt-free posture as `MediaViewerKind.h`): `MediaViewerSortCriterion
  { Name, Duration, Size }` and `MediaViewerViewMode { Thumbnails }`. A separate sort enum rather
  than extending the existing (Application-layer) `SortCriterion` -- `Duration` is meaningless for
  plain file browsing/directories, so it stays scoped to Media Viewer. `MediaViewerViewMode` has a
  single enumerator today and is a deliberate stub for future view modes the user asked to be able
  to add later (e.g. a list view) -- `setMediaViewerViewMode` only stores state and emits a signal
  for the new combo to reflect; the grid itself stays locked to `DisplayMode::MediaViewerGrid`
  regardless.
* **State lives on `TabViewModel`** (`m_mediaViewerSortCriterion`, `m_mediaViewerSortAscending`,
  `m_mediaViewerViewMode`, defaulting to Name/ascending/Thumbnails), per-tab like `m_mediaViewerKind`
  -- UI only dispatches to it, no business logic in the widget.
* **Duration sort reuses §14.62's thumbnail cache.** `FileNode`/the recursive-scan candidates never
  carry a duration (it's only known once `MediaProcessingUseCase::generateThumbnail()` decodes a
  video), so Duration-sort compares against `FileListModel::m_mediaThumbnailsByPath` via a new
  `mediaThumbnailDuration(const QString& path)` accessor instead of adding a new field to `FileNode`.
  An item whose thumbnail hasn't decoded yet always sorts after every item with a known duration
  (regardless of ascending/descending) rather than being treated as a 0ms duration. Because that
  cache fills in progressively, `TabViewModel::requestMediaViewerResultThumbnails`'s batch callback
  re-sorts (`reapplyMediaViewerSort()`) whenever the active criterion is Duration, so the grid
  reorders live as videos finish decoding instead of only once at load.
* **`reapplyMediaViewerSort()`** is a cheap re-order only -- it re-filters the already-fetched
  `m_mediaViewerCandidates` by kind/name (unchanged candidates, no new I/O) and re-sorts, then goes
  through `FileListModel::refilterEntries()` so it never touches icon/thumbnail requests; changing
  sort order never changes which paths need decoding.
* **UI**: `MediaViewerSortViewBar` (`src/ui/widgets/navigation/`), a plain `QWidget` (same shape as
  `AddressBarWidget`, no theme notifier) holding three tooltipped `QComboBox`es in a `QHBoxLayout`,
  added to `WorkspacePaneWidget::createToolBar()` right after `AddressBarWidget` and hidden by
  default; `updateToolBarEnabledForMediaViewer` shows/enables it exactly when Media Viewer mode is
  active (mirroring how it disables everything else), and the pane's tab-bind/unbind lifecycle
  resyncs it from (and forwards edits to) whichever `TabViewModel` is currently bound, the same
  pattern already used for `viewModeChanged`/`mediaViewerModeChanged`.
* **No new Port, no schema change.** Purely Adapters(ViewModel) + UI, consistent with every other
  Media Viewer entry.
* **Testing**: no new GTest coverage, same precedent as §14.61/§14.62/§14.67 (this layer sits under
  the Architecture.md §11 UI/ViewModel-glue carve-out); manual smoke test only.

### 14.69 Media Viewer Mode: Filmstrip + Details View Modes

§14.68 added the view combo but left `MediaViewerViewMode` a deliberate single-enumerator stub —
nothing downstream of `TabViewModel::setMediaViewerViewMode` reconfigured the grid, and
`FileBrowserView::setViewMode` unconditionally locked `DisplayMode::MediaViewerGrid` to the one
icon-grid layout set up at construction. This adds the two promised view modes: **Filmstrip** (one
full-width row per item, every decoded preview frame shown side-by-side, name underneath) and
**Details** (one full-width row per item: hover-scrubbable preview on the left, name/length/size/
resolution/tags on the right). No new Port, no schema change — purely Adapters(ViewModel) + UI,
consistent with every other Media Viewer entry.

* **`MediaViewerViewMode`** gains `Filmstrip`/`Details` alongside `Thumbnails`. `MediaViewerSortViewBar`'s
  view combo gains the two matching items; its `currentIndexChanged` lambda and `setViewMode()`'s
  switch extend to a 3-way mapping (same shape as the sort-criterion combo's existing 3-way one).
* **Both new modes render through the existing `m_listView`**, not a new stack page or the
  `QTreeView` — `QListView::ListMode` + `Flow::TopToBottom` + `setWrapping(false)` (the one
  existing mode that sets `Wrapping(true)`, `ViewMode::List`, deliberately doesn't: it wants
  Explorer's short multi-column list) gives single-column rows that stretch to the full viewport
  width automatically, like a plain `QListWidget` — no gridSize/resize tracking needed.
* **`FileBrowserView`** gains `setMediaViewerViewMode(MediaViewerViewMode)` (no-op outside
  `DisplayMode::MediaViewerGrid`), swapping `m_listView`'s installed delegate and
  flow/wrapping/gridSize/iconSize per mode: `Thumbnails` keeps today's IconMode/grid setup
  (`m_mediaGridDelegate`); `Filmstrip`/`Details` both use the ListMode/no-wrap config above, with
  `m_mediaFilmstripDelegate`/`m_mediaDetailsDelegate` respectively. The constructor's one-time
  `MediaViewerGrid` setup now ends by calling `setMediaViewerViewMode(MediaViewerViewMode::Thumbnails)`
  instead of inlining that branch's configuration twice.
* **`WorkspacePaneWidget::addPageForTab`** connects `TabViewModel::mediaViewerViewModeChanged` to
  the per-tab `mediaViewerBrowserView`'s new `setMediaViewerViewMode` slot, and applies the tab's
  current value once at page-construction time — previously that signal only reached the toolbar
  combo (`MediaViewerSortViewBar::setViewMode`, resync only), never the grid itself, which is why
  the combo had no visible effect before this.
* **`MediaFilmstripDelegate`** (`src/ui/widgets/media/`, new): `QStyledItemDelegate`, fixed-height
  `sizeHint()` (row width comes from the ListView config above, not the delegate). Reuses
  §14.64's `MediaThumbnailData::previewFrames` with no new decode work: a video with a full
  `MediaPreviewFrames::kCount`-frame set paints all of them side-by-side (thin 1px dividers
  between bands); an image, or a video not yet fully decoded, paints its single `image` stretched
  across the whole row. Filename caption centered below a thin divider line, same font/
  foreground-role handling as `MediaGridTileDelegate::paint`; dashed selection outline on
  `State_Selected`, matching every other delegate here.
* **`MediaDetailsDelegate`** (`src/ui/widgets/media/`, new): `QStyledItemDelegate` with hover
  state mirroring `MediaGridTileDelegate`'s shape (`setHoveredPreview`/`hoveredIndex`/
  `hoveredPreviewIndex`, a `thumbnailRectForCellRect` static over a small fixed-size square
  anchored left, vertically centered) — Qt's per-cell single-delegate model means a literal
  shared-delegate-instance reuse isn't possible for two independently-laid-out regions of one
  cell, so this mirrors the API shape and shares the actual pixel-painting code instead (see
  `MediaThumbnailPainting` below) rather than duplicating it a third time. Right side: bold/
  larger-point name; a `duration | size | WxH` line (duration segment and its separator omitted
  for an image row, same `MediaExtensions::isImageExtension` branch `MediaGridTileDelegate`
  already uses; resolution omitted until known); a tag-pill row via `TagPillRowPainter` (below)
  against `FileListModel::TagsRole`.
* **`MediaThumbnailPainting`** (`src/ui/widgets/media/`, new): `paintThumbnail` (aspect-fit
  letterbox)/`paintPlaceholderIcon`/`durationLabel`, lifted unchanged out of
  `MediaGridTileDelegate.cpp`'s anonymous namespace so `MediaFilmstripDelegate`/
  `MediaDetailsDelegate` can reuse the same non-trivial pixel-painting code instead of a third
  copy — unlike the small layout-rect math this codebase deliberately keeps duplicated per
  delegate (§14.62's own comment), real image-scaling/formatting logic earns a shared helper once
  3 call sites need it. `MediaGridTileDelegate` itself is updated to call these instead of its own
  copies; its behavior is unchanged.
* **`TagPillRowPainter`** (`src/ui/widgets/tags/`, new): `paintTagPillRow`/`tagPillRowHeight`,
  lifted out of `TagsCellDelegate.cpp`'s anonymous namespace (the pill-paint loop, including the
  trailing "+N" overflow pill) for the same reason — now shared between `TagsCellDelegate` and
  `MediaDetailsDelegate`. `TagsCellDelegate`'s own behavior is unchanged.
* **`updateMediaViewerHover()`** (FileBrowserView's existing §14.64 hover-scrub handler) becomes
  mode-aware: a no-op while `Filmstrip` is active (every frame is already visible at once, no
  single-frame hover concept); while `Details` is active, resolves the thumbnail sub-rect via
  `MediaDetailsDelegate::thumbnailRectForCellRect` instead of `MediaGridTileDelegate`'s, and
  updates `m_mediaDetailsDelegate`'s hover state instead. The "skip non-video rows"/"repaint only
  on an actual change" logic is otherwise unchanged.
* **Tags weren't fetched for the Media Viewer results model at all before this** —
  `TabViewModel::requestFileTags` was only ever called for the normal-browsing `m_fileListModel`,
  never for `m_mediaViewerResultsModel`, since nothing needed them before Details' tag row.
  `TabViewModel` gains `requestMediaViewerResultTags(generation, candidates)` (mirrors
  `requestSearchResultTags`/`requestAdvancedSearchResultTags`'s existing shape), called from
  `applyMediaViewerFilter()` alongside the existing icon/thumbnail requests and from the
  `TagChangeNotifier::tagsChanged` handler alongside the search/advanced-search tag refreshes —
  feeding `m_mediaViewerResultsModel->setFileTags(...)`.
* **Not built**: persisting the view-mode choice across restarts (same as §14.68's existing
  Thumbnails-only posture); hover-scrub for Filmstrip (see above); any change to the normal
  file-browser's own `ViewMode::Details`/`ViewMode::List` (a separate enum/concept from
  `MediaViewerViewMode`, untouched here).
* **Testing**: no new GTest coverage, same precedent as §14.61/§14.62/§14.64/§14.67/§14.68 (this
  layer sits under the Architecture.md §11 UI/ViewModel-glue carve-out); manual smoke test only.

---
### 14.72 Media Viewer: Clickable Tags with a Filter/Apply Behaviour Toggle

Two Media-Viewer-only tag-click affordances, both gated by one new "Tag behaviour" dropdown
(Filter/Apply) next to the Searcher tab's tag-search box: (1) a plain click vs. a Ctrl+click on a
search-results tag chip now either adds the tag to the filter set or applies it to the current
selection; (2) the Details view's per-item tag pills (previously purely decorative, painted by
`TagPillRowPainter` against `FileListModel::TagsRole`) become clickable, with the same click/
Ctrl+click split adding the tag to the filter or removing it from that one item. Scoped entirely to
Media Viewer mode — the main file-browser panel's own Tag Panel/Advanced Search tag chips are
unaffected. No Domain/Application change: reuses `TagManagementUseCase::assignTag`/`unassignTag`,
`TabViewModel::m_selectedEntries`, and `TagChangeNotifier::tagsChanged()`.

* **The Filter/Apply rule is one XOR, applied identically at both call sites**: a click is a
  "filter" action when `(behaviour == Filter) != ctrlHeld`, otherwise it's the "other" action
  (apply-to-selection for the search list, remove-from-item for the Details pills). Centralized in
  `TabViewModel` rather than duplicated per widget.
* **`MediaViewerTagBehaviour`** (`src/adapters/viewmodels/media/MediaViewerTagBehaviour.h`, new):
  `enum class MediaViewerTagBehaviour { Filter, Apply }`, same minimal posture as
  `MediaViewerKind.h`/`MediaViewerViewMode.h`.
* **`TagChipWidget::clicked`** gains a `Qt::KeyboardModifiers` parameter (now
  `clicked(Tag::Id, Qt::KeyboardModifiers)`), set from `mousePressEvent`'s `event->modifiers()`.
  Every existing connection (`TagPanelWidget`, `TagManagerDialog`, `SearchCriteriaPanel`) keeps
  working unchanged — Qt's `connect` allows a receiving slot/signal to take fewer trailing
  arguments than the emitting signal.
* **`TabViewModel`** gains `mediaViewerTagBehaviour()`/`setMediaViewerTagBehaviour()` (+
  `mediaViewerTagBehaviourChanged` signal) — a per-tab UI preference, **not** reset by
  `enterMediaViewer()`/`exitMediaViewer()`, same posture as `m_mediaViewerSortCriterion`/
  `m_mediaViewerViewMode` — plus three new slots implementing the rule above:
  `handleMediaViewerSearchTagActivated(Tag::Id, bool ctrlHeld)` (search-chip click: filter vs.
  `applyMediaViewerTagToSelection`), `applyMediaViewerTagToSelection(Tag::Id)` (bulk `assignTag`
  over `m_selectedEntries`, ignoring a per-entry `AlreadyExists`; emits `mediaViewerFailed` on an
  empty selection, otherwise a "Tag Applied" popup + `tagsChanged()` on at least one success), and
  `handleMediaViewerDetailsTagClick(const FileNode&, Tag::Id, bool ctrlHeld)` (Details-pill click:
  filter vs. a single `unassignTag` on that entry). All three no-op outside Media Viewer mode.
* **`MediaViewerSearchPanelWidget`** gains the "Tag behaviour" `QComboBox` (`m_tagBehaviourCombo`,
  Filter/Apply) in a new `QHBoxLayout` alongside the tag-search box, `setTagBehaviour()` (resync,
  `QSignalBlocker`-guarded like `setKind`), and a `tagBehaviourChanged` signal. Its old
  `tagFilterAdded` signal (dead — only ever emitted from the search-results chip) is replaced by
  `tagSearchChipActivated(Tag::Id, Qt::KeyboardModifiers)`, a plain relay of the chip's full click
  payload — the widget stays "dumb"; the Filter/Apply decision lives in `TabViewModel` above, same
  posture as `kindChanged`/`tagFilterRemoved`.
* **`TagPillRowPainter`** gains `tagIdAt(rect, tags, font, pos)`, hit-testing the same pill layout
  `paintTagPillRow` paints (a `layoutTagPillRow` helper now computes each pill's rect/text/
  background once, shared by both) — returns the tag under `pos`, or `nullopt` for a miss or for
  the "+N" overflow pill, which isn't associated with any single tag.
* **`MediaDetailsDelegate`** overrides `editorEvent`: on a left-button release, hit-tests the tags
  row via a new `computeTagsRect()` (the tags-row geometry factored out of `paint()` — it doesn't
  depend on `hasThumbnail`, only what's drawn inside it does) + `TagPillRowPainter::tagIdAt`,
  emitting a new `tagActivated(QModelIndex, Tag::Id, Qt::KeyboardModifiers)` signal on a hit.
  `TagsCellDelegate` (the main-panel Details view) is untouched — this is Media-Viewer-only.
* **`FileBrowserView`** gains a `mediaViewerDetailsTagClicked(const FileNode&, Tag::Id, bool
  ctrlHeld)` signal, wired from `MediaDetailsDelegate::tagActivated` in the constructor's
  `DisplayMode::MediaViewerGrid` branch (resolving the row index to a `FileNode` via
  `FileListModel::entryAt`). `m_mediaDetailsDelegate` exists on every instance regardless of
  `DisplayMode`, but only the Media-Viewer-grid instance ever actually installs it as the active
  delegate (hence only it ever fires `editorEvent`), so this connection is harmless elsewhere.
* **Wiring**: `MainWindow` connects `tagSearchChipActivated` to
  `TabViewModel::handleMediaViewerSearchTagActivated` and `tagBehaviourChanged` to
  `setMediaViewerTagBehaviour` (plus the matching per-focused-tab resync pair, mirroring
  `mediaViewerKindChanged`/`setKind`). `WorkspacePaneWidget::wireBrowserView` (shared by all three
  per-tab `FileBrowserView` instances) connects `mediaViewerDetailsTagClicked` straight to
  `TabViewModel::handleMediaViewerDetailsTagClick`.
* **Testing**: no new GTest coverage, same precedent as the rest of this file (Architecture.md §11
  UI/ViewModel-glue carve-out); manual smoke test only.

---
### 14.73 Media Viewer Tag Search: Autocomplete, Create-Tag Affordance, Enter-to-Apply-or-Create

Ports the Tag Panel's keyboard-driven tag-entry pattern (§14.70) onto the Media Viewer Searcher
tab's tag-search box, which until now was a plain `QLineEdit` with no autocomplete, no create-tag
path, and no Enter-key handling at all — tags could only be acted on by clicking an existing
matching-tag chip (§14.72). No Domain/Application change: reuses
`TagManagementUseCase::searchTags`/`createTag`, `TagColorPalette::pickColorFor`, and
`TabViewModel::applyMediaViewerTagToSelection` (§14.72).

* **`MediaViewerSearchPanelWidget::m_tagSearchEdit`** changes type from a plain `QLineEdit` to
  `TagSearchEdit` (`src/ui/widgets/tags/TagSearchEdit.h`, §14.70's autocomplete popup/ghost-suffix/
  Tab-complete `QLineEdit` subclass) — a drop-in swap; the existing `textChanged` wiring to
  `tagSearchQueryRequested` is untouched. `setTagSearchResults(...)` now also pushes the matching
  tags' names into `m_tagSearchEdit->setSuggestions(...)`, same as
  `TagPanelWidget::updateSearchSuggestions`.
* **Create-tag affordance, computed locally rather than ViewModel-pushed**: a new
  `m_createTagButton` (hidden by default, mirrors `TagPanelWidget::m_createTagButton`) is shown
  with the label `Create tag "<text>"` whenever `setTagSearchResults(...)` runs and the search
  edit's current trimmed text has no case-insensitive exact match among the tags just received.
  Unlike `TagListViewModel::canCreateTagFromQueryChanged`, there's no new ViewModel-side boolean
  for this — `MediaViewerSearchPanelWidget` already receives both the live query text (its own
  field) and the latest matches (`setTagSearchResults`'s argument) on every round-trip, so the
  "no exact match" predicate (same one `TagListViewModel::refreshSearchSection` uses) is evaluated
  directly in the widget.
* **Enter-key commit and the create button are both "dumb" relays**, same posture as
  `tagSearchChipActivated` (§14.72) — the widget never decides apply-vs-create itself:
  - `m_tagSearchEdit`'s `returnPressed` emits a new `tagSearchQueryAccepted(const QString&)` with
    the raw typed text, then clears the edit.
  - `m_createTagButton`'s `clicked` emits a new `tagCreateRequested(const QString&)` with the
    trimmed text, then clears the edit.
* **`TabViewModel`** gains two slots implementing the decision, both no-op outside Media Viewer
  mode:
  - `acceptMediaViewerSearchTagQuery(const QString& query)` — re-queries
    `TagManagementUseCase::searchTags` for an exact case-insensitive name match; if found, calls
    `applyMediaViewerTagToSelection(id)` (same bulk-assign-over-`m_selectedEntries` behavior
    §14.72 already uses for chip clicks); otherwise defers to the slot below. Mirrors
    `TagListViewModel::acceptSearchQuery`.
  - `createAndApplyMediaViewerTag(const QString& name)` — creates the tag (color via
    `TagColorPalette::pickColorFor`) and applies it via `applyMediaViewerTagToSelection`; emits
    `mediaViewerFailed` on a `createTag` failure. Mirrors
    `TagListViewModel::createAndAddTagFromQuery`.
  - Deliberate divergence from §14.70: Enter here always **applies to the full current selection**
    (`m_selectedEntries`), never the single resolved target `TagListViewModel` uses, and is
    completely independent of the Filter/Apply behaviour toggle (§14.72) — that toggle only ever
    governs a tag-chip/pill *click*, not this field's Enter key.
* **Wiring**: `MainWindow` connects `tagSearchQueryAccepted` → `TabViewModel::acceptMediaViewerSearchTagQuery`
  and `tagCreateRequested` → `TabViewModel::createAndApplyMediaViewerTag`, both through the same
  `focusedTab()`-lookup lambda style as the rest of this panel's signals.
* **Testing**: no new GTest coverage, same precedent as the rest of this file (Architecture.md §11
  UI/ViewModel-glue carve-out); manual smoke test only.

---
### 14.74 Media Viewer Mode: Folder-Grouped Browsing

§14.61's grid has always been one flat, recursively-gathered list regardless of view mode. This
adds a second grouping mode, scoped to the Thumbnails view only: items are shown one folder at a
time (the entered root, or any subfolder descended into), with a tile per subfolder containing
qualifying media alongside the current folder's own direct media files. Entirely Adapters(ViewModel)/
UI — no new Port, no Domain/Application/schema change — built entirely on data already fetched:
`m_mediaViewerCandidates` (the full recursive, kind/name-filtered set) is simply regrouped in
memory per folder, and `requestKindSweep`'s existing Low-priority decode sweep (§14.66) already
covers every candidate regardless of grouping, so folder tiles' sample thumbnails arrive from the
same cache with no new decode dispatch.

* **`MediaViewerGroupMode`** (`src/adapters/viewmodels/media/MediaViewerGroupMode.h`, new):
  `enum class MediaViewerGroupMode { Recursive, ByFolder }`, same minimal posture as
  `MediaViewerKind.h`. A fourth `QComboBox` on `MediaViewerSortViewBar` drives it, same
  construction/signal/resync shape as the existing three.
* **`TabViewModel`** gains `m_mediaViewerGroupMode` (a toolbar preference like
  `m_mediaViewerSortCriterion`/`m_mediaViewerViewMode` — not reset by enter/exitMediaViewer),
  `m_mediaViewerCurrentFolder` (the folder currently displayed in Folder mode, reset to
  `m_mediaViewerRoot` on `enterMediaViewer()` and whenever group mode switches into `ByFolder`),
  and a second `NavigationHistory m_mediaViewerFolderHistory` instance (the existing plain
  path-only back/forward stack class, unchanged, seeded with the root) scoped entirely to
  Folder-mode sub-browsing — kept completely separate from the tab's real `currentPath()`/`m_history`
  so exiting Media Viewer is unaffected.
* **Folder-confined sub-navigation reuses the real toolbar actions and `AddressBarWidget`**,
  rather than a new breadcrumb widget or the Tree side panel (resolved with the user over the Tree
  panel and a dedicated breadcrumb as alternatives) — this is the one piece of §14.61's navigation
  lock this feature deliberately reverses, and only while `mediaViewerGroupMode() == ByFolder &&
  mediaViewerViewMode() == Thumbnails`. New `TabViewModel` methods mirror the existing
  `navigateTo`/`upTarget`/`goUp`/`goBack`/`goForward`/`peekBack`/`peekForward` shape exactly, but
  operate on `m_mediaViewerCurrentFolder`/`m_mediaViewerFolderHistory` and never touch disk/Ports —
  `mediaViewerNavigateTo(path)` rejects anything outside `isPathWithinOrEqual(path,
  m_mediaViewerRoot)` (`src/domain/PathContainment.h`), and `mediaViewerUpTarget()` clamps at the
  root the same way `upTarget()` clamps at `ThisPC`. `WorkspacePaneWidget`'s
  `onUpRequested`/`onBackRequested`/`onForwardRequested`/address-bar-submit handlers each gain an
  early branch to the `mediaViewer*` equivalents when folder-nav is active instead of the normal
  ones (skipping `confirmLeavingMonitoredScope`, not relevant here); `updateToolBarEnabledForMediaViewer`
  now keeps back/forward/up/address-bar enabled exactly under that same condition instead of
  always disabling them, pulling the shown state from the `mediaViewer*` accessors. A folder tile's
  double-click (`wireBrowserView`'s `itemActivated`) is likewise redirected to
  `mediaViewerNavigateTo(path)` instead of the normal `navigateTo(path)` whenever Folder mode is
  active.
* **`applyMediaViewerFilter()`** branches when `ByFolder` + `Thumbnails` are both active: a new
  private `buildFolderGroupedView()` partitions `filterMediaViewerCandidates()` into the current
  folder's direct-child files (unchanged `FileNode`s) and, for every other still-under-the-current-
  folder candidate, buckets it by its immediate child-of-current-folder ancestor directory,
  synthesizing one `FileType::Directory` `FileNode` per non-empty bucket plus a
  `{ itemCount, up to 4 sample paths }` side record — **recursive**, i.e. covering every qualifying
  file nested anywhere below that subfolder, not just its direct children (resolved with the
  user). The combined folder+file list is sorted via the existing `sortMediaViewerResults()`
  unchanged — `FileNavigationUseCase::sortBy`'s Name/ModificationDate/FileType criteria already
  group directories before files, so folder tiles lead for free under the default Name sort. Only
  the direct-child files are dispatched through `requestMediaViewerResultIcons`/
  `requestMediaViewerResultThumbnails`; folder tiles never need their own path decoded. Any other
  mode combination (`Recursive`, or `ByFolder` outside `Thumbnails`) keeps exactly the prior flat
  behaviour — "works only for Thumbnails view, other views show all items" per the original ask.
* **`FileListModel`** gains a `FolderPreviewRole` and a small `Q_DECLARE_METATYPE`'d value type
  (`{ int itemCount; std::vector<QImage> previewImages; }`) resolved, for a `Directory` row, by
  looking its seed's sample paths up against the *existing* `m_mediaThumbnailsByPath` cache —
  no new decode request path. `setMediaThumbnails()`'s batch-merge gains an extra `dataChanged`
  sweep over `FolderPreviewRole` across currently-held directory rows (cheap — a handful of
  visible folder tiles) so an arriving sample thumbnail backfills any folder tile referencing it,
  same "placeholder now, real data later" relationship every other role here already has.
* **`MediaGridTileDelegate`** branches on a row's `fileType` being `Directory`: a new
  `MediaThumbnailPainting::paintFolderPreviewCollage` (lifted into the same shared-helper home
  §14.69 established) divides the thumbnail rect into a 2×2 grid, reusing the existing
  `paintThumbnail` per quadrant for each available sample image and leaving any still-undecoded
  quadrant as a neutral placeholder; the size/resolution bottom corner chips are skipped (meaningless
  for a folder) and replaced by a new top-right folder-glyph + item-count badge, overlapping the
  collage per the original ask — `paintCornerChip`'s `bool alignRight` parameter is generalized to
  a `Qt::Corner` (four positions) to support the new anchor without a third copy of the chip-paint
  code.
* **Supersedes** §14.61's "Not built: live re-scoping of the grid if the user could somehow
  navigate mid-session (they can't — navigation is locked)" bullet, for the Folder mode +
  Thumbnails combination specifically — every other mode combination keeps that lock exactly as
  before.
* **Not built:** persisting the group-mode choice or current folder across restarts (same posture
  as every other Media Viewer toolbar preference); any change to normal browsing's or search
  results' own directory-row rendering; a true breadcrumb trail widget (the real address bar
  fills that role instead, per the resolved design above).
* **Testing**: no new GTest coverage, same precedent as the rest of this file (Architecture.md §11
  UI/ViewModel-glue carve-out); manual smoke test only.

---
### 14.75 Media Viewer Mode: Inline Rename Support

§14.61 explicitly disabled rename in Media Viewer mode (`FileBrowserView::beginRename()`/the F2
handler both no-op outside `DisplayMode::Normal`), on the premise this grid is read-only browsing.
This adds real inline rename across all three view modes (§14.68/§14.69's Thumbnails/Filmstrip/
Details), reusing `FileOperationsController::renamePath` via the existing displayMode-agnostic
`FileBrowserView::renameRequested` wiring — no new Port, no Domain/Application/schema change.

* **`MediaGridTileDelegate`/`MediaFilmstripDelegate`/`MediaDetailsDelegate`** change base class
  from plain `QStyledItemDelegate` to `FileNameEditDelegate`, the same pattern `FileIconDelegate`/
  `FileTileDelegate` already use for Normal mode's icon/tile views — gaining prefill-with-stem-
  selected (Explorer-style) editing, the forbidden-char validator, and the `renameCommitted` signal
  for free. Each gains a public, cell-rect-based name-rect static mirroring the
  `thumbnailRectForCellRect` precedent §14.64/§14.69 already established for hover-band math (same
  reasoning: so both the painted text and the editor geometry/click-hit-test agree on one rect) —
  `MediaGridTileDelegate::nameRectForCellRect`, `MediaFilmstripDelegate::captionRectForCellRect`,
  `MediaDetailsDelegate::nameRectForCellRect` — plus an `updateEditorGeometry()` override
  positioning the editor at exactly that rect, same one-line pattern as
  `FileIconDelegate::updateEditorGeometry`.
* **Filmstrip's caption moves from centered to left-aligned** (`paint()`'s `drawText` flag changes
  from `Qt::AlignHCenter` to `Qt::AlignLeft`) per the resolved design — Thumbnails (centered under
  the preview) and Details (top-right of the row, already true of its existing name-line position)
  needed no repositioning, only the new editor wiring.
* **Click-to-rename is restricted to the name text itself**, not the whole tile/row, without
  reimplementing Qt's own click/double-click disambiguation — Normal mode's existing
  `SelectedClicked`-based click-to-rename already proves Qt's trigger machinery doesn't race a
  double-click-to-activate, so a new `MediaViewerGridListView` (a small `QListView` subclass local
  to `FileBrowserView.cpp`, swapped in for `m_listView` only in `DisplayMode::MediaViewerGrid`)
  overrides the protected `edit(index, trigger, event)` virtual to reject (`return false`) a
  `SelectedClicked`-triggered call whose click position misses the current view mode's name rect
  (via a new public `FileBrowserView::mediaViewerNameRectFor(index)`), while leaving every other
  trigger (F2, context-menu, `beginRename()`'s direct `edit()` call — all `AllEditTriggers`)
  unrestricted. This also covers the "a double-click must not start a rename" requirement for free,
  the same way Normal mode's identical, unrestricted `SelectedClicked` trigger already does.
* **`beginRename()`/the F2 handler's guard** widens from "only `DisplayMode::Normal`" to "anything
  except `DisplayMode::AdvancedSearchResults`" — so the context menu's already-built "Rename"
  action and F2 both start working in Media Viewer mode instead of silently no-oping; search
  results remain rename-disabled, unchanged.
* **`FileNameEditDelegate::nameCollidesWithSibling()` bug fix**: it previously treated every row in
  the model as a same-directory sibling (true by construction for Normal mode's single-directory
  listing, so harmless there) — Media Viewer's results model is a recursively-gathered,
  potentially-multi-folder flat list, so two unrelated files in different folders sharing a
  filename would have false-positive blocked a valid rename. Fixed to also compare each candidate's
  parent directory (via `FileListModel::FilePathRole`) against the renamed row's own, with no
  behavior change for Normal mode.
* **Not built**: no change to Normal mode's existing "click anywhere in the icon/tile cell"
  rename-trigger behavior (out of scope — this is Media-Viewer-only); no special-casing of
  Folder-Grouped Browsing's (§14.74) synthesized folder tiles — renaming one renames the real
  underlying subfolder, same as any other directory row.
* **Testing**: no new GTest coverage, same precedent as the rest of this file (Architecture.md §11
  UI/ViewModel-glue carve-out; `FileNameEditDelegate` itself has no dedicated test either); manual
  smoke test only.
* **Follow-up bug fix**: a committed rename left the tile showing its old name. `WorkspaceController`'s
  existing rename funnel (`directoryContentsMayHaveChanged`/`refreshTabsShowing`) only re-lists a
  tab's *normal* `FileListModel` for its current directory — Media Viewer's `m_mediaViewerCandidates`/
  `m_mediaViewerResultsModel` are a separate cached snapshot (this section's own intro) that re-list
  never touches, and is otherwise only refreshed when the applied tag set changes. Fixed by a new
  `TabViewModel::renameMediaViewerEntry(source, destination)`, called from
  `WorkspaceController::onPathRenamed` for every tab alongside its existing `currentPath()` rebase:
  no-ops outside Media Viewer mode, else rebases every cached candidate (and `m_mediaViewerCurrentFolder`,
  for a renamed ancestor folder in Folder-Grouped mode) via `PathContainment.h`'s `rebasePath`/
  `isPathWithinOrEqual` and re-filters in memory only (`applyMediaViewerFilter(false)`) — never a
  rescan, same posture as the sort/filter mutators above. Added `FileNode::withPath()`, mirroring
  `withHash`/`withDisplayName`.

---
### 14.76 Media Viewer Mode: Full-Workspace Display While Active

§14.61's stack-page swap only replaced content inside the one `WorkspacePaneWidget` that owns the
tab entering Media Viewer — correct in `SplitLayout::Single`, but in `TwoVertical`/`TwoHorizontal`/
`FourGrid` the grid was squeezed into one of 2-4 visible panes while sibling panes kept showing
ordinary browsing beside it. This makes Media Viewer take over the whole workspace area regardless
of the active split, and locks split switching for the duration. Entirely UI — no new Port, no
Domain/Application/schema change, and no change to `WorkspaceController`'s own `SplitLayout` state
(so session persistence, §14.14, is unaffected).

* **`WorkspaceLayoutWidget`** gains a display-only override, orthogonal to
  `WorkspaceController::layout()`: `std::optional<WorkspacePaneId> m_forcedSinglePane` plus
  `forceSinglePane(WorkspacePaneId)`/`clearForcedSinglePane()`, both just setting the member and
  re-running the existing `applyLayout(m_controller->layout())` + `updatePaneActiveStates()`.
  `applyLayout`/`updatePaneActiveStates` compute their visible-pane set from
  `{ *m_forcedSinglePane }` instead of `WorkspaceLayoutTopology::visiblePanes(layout)` whenever the
  override is set; `buildContent` likewise short-circuits to returning that one pane directly,
  mirroring exactly what the existing `SplitLayout::Single` case already does for `PaneA`, just
  parameterized by the forced id. No new `SplitLayout` enumerator and no 5th pane — the override
  reuses the focused pane's own existing `WorkspacePaneWidget`, which already owns the
  `MediaViewerGrid` stack page and the per-pane toolbar lock from §14.61; only *which pane(s) are
  visible* changes, not how Media Viewer itself renders.
* **`MainWindow::onMediaViewerModeChanged`** (the existing handler for the focused tab's
  `mediaViewerModeChanged`, already toggling the left/right panels) additionally calls
  `m_workspaceLayoutWidget->forceSinglePane(m_workspaceController->focusedPane())` on entry (Media
  Viewer is always entered on the focused tab, hence the focused pane) and
  `clearForcedSinglePane()` on exit. Split-layout switching is locked for the same duration via
  `m_layoutActionGroup->setEnabled(false/true)` — `QActionGroup::setEnabled` already propagates to
  every action in the group, disabling both the Layout toolbar's 4 buttons and the View ▸ Layout
  submenu's 4 items (the same `QAction` instances feed both) in one call.
* **Not built**: persisting the forced-single-pane state (purely transient, same posture as every
  other Media Viewer transient); any change to per-pane toolbar/tab locking
  (`WorkspacePaneWidget::updateToolBarEnabledForMediaViewer`, §14.61) — unaffected, still scoped to
  the one pane.
* **Testing**: no new GTest coverage, same precedent as the rest of this file (Architecture.md §11
  UI/ViewModel-glue carve-out); manual smoke test only.

---
### 14.77 Media Viewer: Batched Multi-Frame Video Thumbnail Decode (Performance)

§14.64's 5 hover-scrub frames per video are each requested via a separate
`IMediaDecoder::generateThumbnail()` call. Since every call independently did
`avformat_open_input`/`avformat_find_stream_info`/`avcodec_open2` from scratch, a cache-cold video
paid that demux/probe cost 5 times, plus a fresh `sws`/MJPEG-encoder context per frame despite
every frame sharing the same target dimensions. This adds a batched Port method that opens the
container once and decodes every requested frame from that one session. No Domain/UI change;
touches the `IMediaDecoder` Port and `MediaProcessingUseCase` (Application layer) plus their
Adapters implementations — same posture as §14.63's earlier `generateThumbnail`/`extractMetadata`
merge, which also touched the Port for a decode-cost reason.

* **`IMediaDecoder`** (`src/application/media/`) gains
  `generateThumbnails(mediaFile, maxWidth, maxHeight, previewIndices) -> std::vector<Result<MediaThumbnail>>`,
  non-pure with a default body (new `IMediaDecoder.cpp`) that loops the existing
  `generateThumbnail()` once per index — correct for any decoder, but only one with an actual
  open/probe cost to amortize needs to override it. `MediaProcessingUseCase` gains a matching
  method, applying the same `maxWidth/maxHeight > 0`/`previewIndex` range validation as the
  single-frame method, per index.
* **`FFmpegMediaDecoder`** overrides `generateThumbnails`: opens/probes the container and builds
  `MediaMetadata` once, then loops the requested indices doing only the seek →
  `avcodec_flush_buffers` → decode → `sws_scale` → MJPEG-encode steps per frame, reusing one
  `SwsContext` and one MJPEG `AVCodecContext` across the whole batch (dimensions are constant per
  call). A single index's decode/encode failure yields a `Result::failure` for that slot only, so
  one bad seek doesn't fail the rest of the batch — same per-frame-failure tolerance
  `MediaThumbnailLoadingWorker`'s existing neighbor-backfill logic already expects. The existing
  single-frame `generateThumbnail()` is now a one-line wrapper calling `generateThumbnails(...)`
  with a single-element index list.
* **`CachingMediaDecoder`** overrides `generateThumbnails`: checks the cache per requested index
  first, then calls `m_inner.generateThumbnails()` exactly once for whatever indices missed (an
  all-cache-hit batch never touches the inner decoder at all; a partially-warm batch — e.g. 2 of 5
  frames already cached — still only reopens the container once, for the 3 misses), caching each
  newly-generated result before returning the merged, input-ordered vector.
* **`MediaThumbnailLoadingWorker::decodeVideoThumbnail`** replaces its 5 separate
  `generateThumbnail(path, w, h, i)` calls with one `generateThumbnails(path, w, h, {0,1,2,3,4})`
  call; the existing per-slot `QImage` decode, metadata capture, and nearest-neighbor backfill for
  a failed slot are unchanged, just indexed into the returned vector.
* **`VipsImageDecoder`** is untouched — inherits the default loop-based `generateThumbnails`
  (never exercised by current call sites, since images only ever go through the single-frame path,
  but correct if ever called).
* **Not built**: no change to the existing single-frame `generateThumbnail()` call sites
  (`FilePreviewUseCase`'s Preview-panel calls keep working unchanged); no cross-video batching
  (each video's 5 frames are one batch; separate videos are still separate calls — see §14.78 for
  how separate videos get decoded concurrently instead).
* **Testing**: `MockMediaDecoder`/the local `MockInnerDecoder` in `CachingMediaDecoderTest` gain a
  `generateThumbnails` mock method; `FFmpegMediaDecoderTest` gains missing-file/garbage-file cases
  for the batched method (same "fails cleanly" shape as its existing single-frame cases — no real
  video fixture exists in this suite); `CachingMediaDecoderTest` gains cases asserting a mixed
  hit/miss batch calls the inner decoder's `generateThumbnails` exactly once with only the missing
  indices, and that an all-cached batch never calls it; `MediaProcessingUseCaseTest` gains cases for
  the new method's validation and forwarding. No new coverage for
  `MediaThumbnailLoadingWorker` (no existing tests either, same §11 carve-out as the rest of this
  file).

### 14.78 Media Viewer: Parallel Thumbnail Decode Pool (Performance)

§14.62 gave `MediaThumbnailLoadingService` one dedicated worker `QThread`, reasoning that a pool
would "just contend for the same decoder/cache rather than parallelizing real work" — the same
language `IconLoadingService`'s own class comment uses to justify its pool for a different reason
(many cheap synchronous shell/GDI calls). That reasoning doesn't hold for FFmpeg video decode
specifically: each `generateThumbnail`/`generateThumbnails` call (§14.77) is CPU-bound and
self-contained — its own `AVFormatContext`/decoder — and the only state actually shared across
calls, `ThumbnailCache`, is already `std::mutex`-guarded (§8). A folder full of videos was
decoding them one at a time regardless of core count. This replaces the single worker thread with
a small fixed pool, mirroring `IconLoadingService`'s existing pool pattern. No Port, Domain, or
Application change — entirely Adapters(ViewModel).

* **`MediaThumbnailLoadingService`** (`src/adapters/viewmodels/media/`) replaces its single
  `QThread`/`MediaThumbnailLoadingWorker*` with `std::vector<PoolWorker> m_pool` (same
  `{ QThread* thread; MediaThumbnailLoadingWorker* worker; }` shape as
  `IconLoadingService::PoolWorker`) plus a `std::deque<std::size_t> m_idleWorkers` of free pool
  indices, both guarded by the existing `m_pendingMutex`. Pool size uses the same
  `std::clamp(QThread::idealThreadCount() / 2, 2, 4)` formula as
  `IconLoadingService::poolSize()`, leaving headroom for the UI thread and every other background
  service. Each pool worker is a fresh `MediaThumbnailLoadingWorker` sharing the one injected
  `MediaProcessingUseCase&` — safe because the worker holds no per-instance state of its own,
  simpler than `IconLoadingWorker`'s pool (which keeps an independent per-worker icon cache).
* **`startNextIfIdle()`** becomes a loop instead of a single dispatch: while `m_pending` and
  `m_idleWorkers` are both non-empty, pop one of each and dispatch — same two-hop
  `QMetaObject::invokeMethod` shape as before (a staleness check on the worker thread, then
  `Qt::QueuedConnection` back to `this` for the batch callback), just targeting
  `m_pool[workerIndex].worker`. The completion closure returns that worker's index to
  `m_idleWorkers` before looping `startNextIfIdle()` again, replacing the old single `m_processing`
  bool.
* **§14.66's High/Low priority-deque logic in `requestThumbnails`** (High purges this context's
  still-queued High chunks and pushes to the front; Low appends to the back) is unchanged — it only
  ever manipulates the single shared `m_pending` deque, which now simply feeds every idle pool
  worker instead of one.
* **Not built**: no change to `MediaThumbnailLoadingWorker` itself beyond its class comment (already
  stateless, so no per-worker cache to duplicate the way `IconLoadingWorker` does); no dynamic pool
  resizing; no cross-service thread budget coordination with `IconLoadingService`'s own pool (both
  size independently off `QThread::idealThreadCount()`, same as today).
* **Testing**: no new GTest coverage — this layer has none today either (§11 UI/ViewModel-glue
  carve-out, same precedent as every other entry in this file); manual smoke test only (hover/scrub
  several videos at once, and open a video-heavy folder in Media Viewer mode to confirm thumbnails
  now populate concurrently rather than strictly one at a time).

---
### 14.80 Media Viewer: Animations Kind

`MediaViewerKind` gains a third value, `Animation`, giving GIFs their own dropdown bucket separate
from static images (§14.79 added real GIF/SVG support app-wide; this is Media Viewer mode's own
kind-filtering catching up to it). SVG simply joins the existing Images bucket for free, since it's
now `isImageExtension() == true` like every other raster format (§14.79). No Port, Domain, or
Application change — entirely Adapters(ViewModel)/UI, same posture as every other entry in this
file.

* **`MediaViewerKind`** (`src/adapters/viewmodels/media`) gains `Animation` alongside `Image`/
  `Video`.
* **`MediaViewerSearchPanelWidget`**'s kind combo gains a third item, "Animations" (index 2); its
  `currentIndexChanged` lambda and `setKind()` both move from a binary ternary to a 3-way mapping
  (`0 → Image, 1 → Video, 2 → Animation`).
* **`TabViewModel`**'s kind-match logic (`filterMediaViewerCandidates()`,
  `requestKindSweep()`) moves from the same binary `Image ? isImageExtension : isVideoExtension`
  ternary (duplicated in both places) to a shared 3-way match: Image = `isImageExtension &&
  !isAnimatedImageExtension` (carving GIF out, SVG stays in), Animation =
  `isAnimatedImageExtension`, Video unchanged. `m_mediaViewerImageSweepRequested`/
  `m_mediaViewerVideoSweepRequested` (§14.66) gain a third sibling,
  `m_mediaViewerAnimationSweepRequested`, reset alongside the other two in
  `onMediaViewerCandidatesReady()`/`exitMediaViewer()`.
* **Not built**: no delegate change — `MediaGridTileDelegate`/`MediaFilmstripDelegate`/
  `MediaDetailsDelegate` already derive image-vs-video purely from extension (not from
  `MediaViewerKind`), so GIF tiles keep rendering exactly as before (static WxH thumbnail chip, no
  per-tile animation) regardless of which dropdown bucket they're filtered into; no animated grid-
  tile playback.
* **Testing**: no new GTest coverage — same UI/ViewModel-glue carve-out as every other entry in this
  file.

---
### 14.82 Bug Fix: Transparent Thumbnails Invisible Against Black Letterbox Fill

`MediaThumbnailPainting::paintThumbnail()` (§14.69, shared by `MediaGridTileDelegate`/
`MediaFilmstripDelegate`/`MediaDetailsDelegate`) always filled its whole target rect solid black
before drawing the aspect-fit thumbnail on top — originally harmless, since every thumbnail
reaching it came from `VipsImageDecoder`/`FFmpegMediaDecoder`, both of which flatten any source
alpha away (the former composites real transparency onto its own checkerboard before JPEG-encoding
specifically so a transparent-but-dark subject stays visible, §14.25; the latter's MJPEG frame grab
has no alpha at all). `QtSvgImageDecoder` (§14.79) and `QtGifImageDecoder` (§14.81) broke that
assumption: both keep a thumbnail's real alpha channel (PNG-encoded, no checkerboard compositing of
their own), so any transparent SVG/GIF with dark content painted here vanished into the solid black
fill — invisible dark-on-black, the exact failure mode `VipsImageDecoder`'s own checkerboard trick
already exists to avoid.

* **`MediaThumbnailPainting.cpp`** gains a small checkerboard brush (same 8px light-gray/white tile
  pattern, same colour constants, as `VipsImageDecoder`'s `buildCheckerboardTilePixels`, built once
  as a static `QPixmap` rather than composited per-pixel per paint) and `paintThumbnail()` now fills
  with it instead of solid black whenever `image.hasAlphaChannel()` is true — an opaque thumbnail
  (every existing image/video case) keeps the exact same black fill as before, so no visual change
  for anything already working.
* **Not built**: no parameter threading kind/extension info into `paintThumbnail()` itself — the
  `hasAlphaChannel()` check is a precise, call-site-free signal (only a decoder that deliberately
  preserves transparency ever produces one), so every one of the 3 call sites plus
  `paintFolderPreviewCollage()`'s per-quadrant calls needed no change; no change to the Preview
  tab's `MediaPreviewWidget` (unaffected — it draws straight onto the panel's own background, never
  a forced black fill).
* **Testing**: no new GTest coverage — `MediaThumbnailPainting` has none today either (pure Qt
  painting code, same UI-glue carve-out as the rest of this file); manual smoke test only.

