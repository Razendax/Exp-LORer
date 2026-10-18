#include "TabMediaViewerViewModel.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>

#include "AdvancedSearchService.h"
#include "Error.h"
#include "FileNavigationUseCase.h"
#include "FileTagsService.h"
#include "IconLoadingService.h"
#include "IconSizeMask.h"
#include "MediaExtensions.h"
#include "NotificationType.h"
#include "PathContainment.h"
#include "PathHash.h"
#include "TagChangeNotifier.h"
#include "TagColorPalette.h"
#include "TagManagementUseCase.h"

namespace
{
    // Shared by filterCandidates() and requestKindSweep() (Architecture.md §14.80) so the three
    // buckets can't drift out of sync: Animation (GIF) is carved out of what would otherwise be
    // the Image bucket, so Image itself excludes it.
    bool mediaViewerKindMatches(const std::string& extension, MediaViewerKind kind)
    {
        switch (kind)
        {
        case MediaViewerKind::Image:
            return MediaExtensions::isImageExtension(extension) && !MediaExtensions::isAnimatedImageExtension(extension);
        case MediaViewerKind::Video:
            return MediaExtensions::isVideoExtension(extension);
        case MediaViewerKind::Animation:
            return MediaExtensions::isAnimatedImageExtension(extension);
        }
        return false;
    }
}

TabMediaViewerViewModel::TabMediaViewerViewModel(AdvancedSearchService& advancedSearchService,
                                                   TagManagementUseCase& tagManagementUseCase, FileTagsService& fileTagsService,
                                                   IconLoadingService& iconLoadingService,
                                                   MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                                                   TagChangeNotifier& tagChangeNotifier, NotificationCenter& notificationCenter,
                                                   const FileDecorationRules& fileDecorationRules,
                                                   QObject& fileDecorationsChangeSource, QObject* parent)
    : QObject(parent)
    , m_advancedSearchService(advancedSearchService)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileTagsService(fileTagsService)
    , m_iconLoadingService(iconLoadingService)
    , m_mediaThumbnailLoadingService(mediaThumbnailLoadingService)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_notificationCenter(notificationCenter)
    , m_resultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
{
    connect(&m_tagChangeNotifier, &TagChangeNotifier::tagsChanged, this, [this]() {
        if (!m_active)
        {
            return;
        }

        requestResultTags(m_generation, m_resultsModel->entries());
    });

    // Entering Media Viewer mode's own progress-bar card (Architecture.md §14.65) -- only this
    // tab's own in-flight notification cares about a Cancel click, same filter-by-id posture
    // FragmentationViewModel/FolderSizeViewModel use.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this, [this](NotificationCenter::ProgressId id) {
        if (m_progressId != 0 && id == m_progressId)
        {
            exit();
        }
    });
}

TabMediaViewerViewModel::~TabMediaViewerViewModel()
{
    // Defensive guard -- exit()/WorkspacePaneViewModel::closeTab normally terminate this first,
    // but cover any other destruction path too (Architecture.md §14.65).
    if (m_progressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_progressId);
    }
}

void TabMediaViewerViewModel::enter(const std::filesystem::path& currentPath)
{
    if (m_active)
    {
        return;
    }

    m_root = currentPath;
    m_kind = MediaViewerKind::Image;
    m_tagIds.clear();
    m_nameQuery.clear();
    m_nameOptions = NameSearchOptions();
    m_candidates.clear();

    // Folder-Grouped Browsing's own navigation state (Architecture.md §14.74) -- reset
    // unconditionally regardless of m_groupMode's current value (a toolbar preference that
    // persists across sessions), so a session left browsing a deep subfolder last time starts
    // back at the root this time.
    m_currentFolder = m_root;
    m_folderHistory = NavigationHistory();
    m_folderHistory.navigate(m_root);

    m_active = true;
    emit modeChanged(true);
    // The kind is force-reset to Image above without going through setKind (which no-ops when
    // the new kind already equals the old one) -- emit explicitly so MediaViewerSearchPanelWidget's
    // combo resyncs even when it was left showing Video from a previous Media Viewer session on
    // this tab.
    emit kindChanged(MediaViewerKind::Image);

    run();
}

void TabMediaViewerViewModel::exit()
{
    if (!m_active)
    {
        return;
    }

    m_active = false;
    m_root.clear();
    m_tagIds.clear();
    m_nameQuery.clear();
    m_nameOptions = NameSearchOptions();
    m_candidates.clear();
    m_currentFolder.clear();
    m_folderHistory = NavigationHistory();
    // Invalidates any fetch still in flight -- its (now-stale) result is discarded by
    // onCandidatesReady's generation check when it eventually arrives.
    ++m_generation;

    // Covers both the notification's own Cancel button and the user manually toggling Media
    // Viewer mode off mid-load (Architecture.md §14.65) -- "discard, not abort": any in-flight
    // scan/decode batch for the now-stale generation is simply ignored when it lands.
    if (m_progressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_progressId);
        m_progressId = 0;
    }
    m_thumbnailsTotal = 0;
    m_thumbnailsDone = 0;
    m_countedPaths.clear();
    m_imageSweepRequested = false;
    m_videoSweepRequested = false;
    m_animationSweepRequested = false;

    emit modeChanged(false);
}

void TabMediaViewerViewModel::renameEntry(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    if (!m_active)
    {
        return;
    }

    bool changed = false;
    for (FileNode& candidate : m_candidates)
    {
        if (isPathWithinOrEqual(candidate.path(), source))
        {
            candidate = candidate.withPath(rebasePath(candidate.path(), source, destination));
            changed = true;
        }
    }

    if (!changed)
    {
        return;
    }

    if (isPathWithinOrEqual(m_currentFolder, source))
    {
        m_currentFolder = rebasePath(m_currentFolder, source, destination);
    }

    applyFilter(false);
}

void TabMediaViewerViewModel::setKind(MediaViewerKind kind)
{
    if (!m_active || kind == m_kind)
    {
        return;
    }

    m_kind = kind;
    applyFilter(false);
    // Switching to a kind nobody has looked at yet this generation actually has to decode it
    // (Architecture.md §14.66) -- requestKindSweep no-ops if `kind`'s sweep was already dispatched
    // (e.g. switching back to a kind seen earlier this generation is a cheap re-filter only).
    requestKindSweep(m_generation, kind);
    emit kindChanged(kind);
}

void TabMediaViewerViewModel::addTagFilter(Tag::Id tagId)
{
    if (!m_active || std::find(m_tagIds.begin(), m_tagIds.end(), tagId) != m_tagIds.end())
    {
        return;
    }

    m_tagIds.push_back(tagId);
    run();
    emit criteriaChanged();
}

void TabMediaViewerViewModel::removeTagFilter(Tag::Id tagId)
{
    if (!m_active)
    {
        return;
    }

    const auto it = std::find(m_tagIds.begin(), m_tagIds.end(), tagId);
    if (it == m_tagIds.end())
    {
        return;
    }

    m_tagIds.erase(it);
    run();
    emit criteriaChanged();
}

void TabMediaViewerViewModel::setNameFilter(const QString& query, const NameSearchOptions& options)
{
    if (!m_active)
    {
        return;
    }

    m_nameQuery = query;
    m_nameOptions = options;
    applyFilter(false);
}

void TabMediaViewerViewModel::setSortCriterion(MediaViewerSortCriterion criterion)
{
    if (criterion == m_sortCriterion)
    {
        return;
    }

    m_sortCriterion = criterion;
    emit sortCriterionChanged(criterion);
    reapplySort();
}

void TabMediaViewerViewModel::setSortAscending(bool ascending)
{
    if (ascending == m_sortAscending)
    {
        return;
    }

    m_sortAscending = ascending;
    emit sortAscendingChanged(ascending);
    reapplySort();
}

void TabMediaViewerViewModel::setViewMode(MediaViewerViewMode mode)
{
    if (mode == m_viewMode)
    {
        return;
    }

    m_viewMode = mode;
    emit viewModeChanged(mode);
    reapplySort();
}

void TabMediaViewerViewModel::setGroupMode(MediaViewerGroupMode mode)
{
    if (!m_active || mode == m_groupMode)
    {
        return;
    }

    m_groupMode = mode;

    // Neither direction touches currentFolder()/the folder-nav history -- switching
    // Recursive<->ByFolder preserves wherever Folder-mode browsing currently is (Architecture.md
    // §14.74), rather than snapping Folder mode back to the root every time it's re-selected.

    applyFilter(false);
    emit groupModeChanged(mode);
}

void TabMediaViewerViewModel::setPreviewMode(MediaViewerPreviewMode mode)
{
    if (!m_active || mode == m_previewMode)
    {
        return;
    }

    m_previewMode = mode;
    emit previewModeChanged(mode);
}

std::filesystem::path TabMediaViewerViewModel::upTarget() const
{
    if (pathsEqualCaseInsensitive(m_currentFolder, m_root))
    {
        // No legal parent within the confined subtree -- returning the current folder itself
        // signals "nothing to navigate to", mirroring TabViewModel::upTarget()'s ThisPC clamp.
        return m_currentFolder;
    }

    return m_currentFolder.parent_path();
}

std::optional<std::filesystem::path> TabMediaViewerViewModel::peekBack() const
{
    return m_folderHistory.peekBack();
}

std::optional<std::filesystem::path> TabMediaViewerViewModel::peekForward() const
{
    return m_folderHistory.peekForward();
}

void TabMediaViewerViewModel::navigateTo(const std::filesystem::path& path)
{
    if (!m_active || !isPathWithinOrEqual(path, m_root))
    {
        return;
    }

    m_folderHistory.navigate(path);
    m_currentFolder = path;
    applyFilter(false);

    emit currentFolderChanged(m_currentFolder);
    emitFolderAvailability();
}

void TabMediaViewerViewModel::goUp()
{
    const std::filesystem::path target = upTarget();
    if (target == m_currentFolder)
    {
        return;
    }

    navigateTo(target);
}

void TabMediaViewerViewModel::goBack()
{
    const auto path = m_folderHistory.goBack();
    if (!path)
    {
        return;
    }

    m_currentFolder = *path;
    applyFilter(false);

    emit currentFolderChanged(m_currentFolder);
    emitFolderAvailability();
}

void TabMediaViewerViewModel::goForward()
{
    const auto path = m_folderHistory.goForward();
    if (!path)
    {
        return;
    }

    m_currentFolder = *path;
    applyFilter(false);

    emit currentFolderChanged(m_currentFolder);
    emitFolderAvailability();
}

void TabMediaViewerViewModel::setTagBehaviour(MediaViewerTagBehaviour behaviour)
{
    if (behaviour == m_tagBehaviour)
    {
        return;
    }

    m_tagBehaviour = behaviour;
    emit tagBehaviourChanged(behaviour);
}

void TabMediaViewerViewModel::handleSearchTagActivated(Tag::Id tagId, bool ctrlHeld, const std::vector<FileNode>& selectedEntries)
{
    if (!m_active)
    {
        return;
    }

    if ((m_tagBehaviour == MediaViewerTagBehaviour::Filter) != ctrlHeld)
    {
        addTagFilter(tagId);
    }
    else
    {
        applyTagToSelection(tagId, selectedEntries);
    }
}

void TabMediaViewerViewModel::applyTagToSelection(Tag::Id tagId, const std::vector<FileNode>& selectedEntries)
{
    if (selectedEntries.empty())
    {
        emit failed(tr("No items selected to apply the tag to."));
        return;
    }

    std::size_t successCount = 0;
    for (const FileNode& entry : selectedEntries)
    {
        auto result = m_tagManagementUseCase.assignTag(entry, tagId);
        if (result || result.error().code == ErrorCode::AlreadyExists)
        {
            ++successCount;
        }
    }

    if (successCount == 0)
    {
        return;
    }

    m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Applied"), tr("Tagged %1 item(s).").arg(successCount));
    emit m_tagChangeNotifier.tagsChanged();
}

void TabMediaViewerViewModel::acceptSearchTagQuery(const QString& query, const std::vector<FileNode>& selectedEntries)
{
    if (!m_active)
    {
        return;
    }

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    auto searchResult = m_tagManagementUseCase.searchTags(trimmed.toStdString());
    const std::vector<Tag> results = searchResult ? searchResult.value() : std::vector<Tag>{};
    const auto it = std::find_if(results.begin(), results.end(), [&trimmed](const Tag& tag) {
        return QString::fromStdString(tag.name()).compare(trimmed, Qt::CaseInsensitive) == 0;
    });
    if (it != results.end())
    {
        applyTagToSelection(it->id(), selectedEntries);
    }
    else
    {
        createAndApplyTag(trimmed, selectedEntries);
    }
}

void TabMediaViewerViewModel::createAndApplyTag(const QString& name, const std::vector<FileNode>& selectedEntries)
{
    if (!m_active)
    {
        return;
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    const std::string colorHex = TagColorPalette::pickColorFor(trimmed.toStdString());
    auto created = m_tagManagementUseCase.createTag(trimmed.toStdString(), colorHex);
    if (!created)
    {
        emit failed(QString::fromStdString(created.error().message));
        return;
    }

    applyTagToSelection(created.value().id(), selectedEntries);
}

void TabMediaViewerViewModel::handleDetailsTagClick(const FileNode& entry, Tag::Id tagId, bool ctrlHeld)
{
    if (!m_active)
    {
        return;
    }

    if ((m_tagBehaviour == MediaViewerTagBehaviour::Filter) != ctrlHeld)
    {
        addTagFilter(tagId);
        return;
    }

    auto result = m_tagManagementUseCase.unassignTag(entry, tagId);
    if (!result)
    {
        emit failed(QString::fromStdString(result.error().message));
        return;
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TabMediaViewerViewModel::run()
{
    const int generation = ++m_generation;

    emit loading();

    // A tag-filter change re-querying while a previous scan/decode is still in flight terminates
    // that notification first, rather than leaving it to onCandidatesReady/the destructor --
    // there would otherwise be a window where a stale bar from the superseded generation is
    // still visible (Architecture.md §14.65).
    if (m_progressId != 0)
    {
        m_notificationCenter.terminateProgressNotification(m_progressId);
    }
    m_thumbnailsTotal = 0;
    m_thumbnailsDone = 0;
    m_countedPaths.clear();
    m_progressId = m_notificationCenter.createProgressNotification(tr("Loading Media Viewer..."), /*barCount=*/1, /*cancellable=*/true);
    m_notificationCenter.updateProgressBar(m_progressId, 0, 0, 0, tr("Scanning folder..."), QString(), QString());

    if (!m_tagIds.empty())
    {
        // No onProgress hook on this (typically much faster, DB-driven) path -- it keeps the
        // static "Scanning folder..." text set above.
        m_advancedSearchService.requestTagFirstCandidates(this, m_root, m_tagIds, [this, generation](Result<std::vector<FileNode>> result) {
            onCandidatesReady(generation, std::move(result));
        });
        return;
    }

    m_advancedSearchService.requestRecursiveScan(
        this, m_root, [this, generation](Result<std::vector<FileNode>> result) { onCandidatesReady(generation, std::move(result)); },
        [this, generation](std::vector<FileNode> partial) {
            if (generation != m_generation)
            {
                return;
            }

            m_notificationCenter.updateProgressBar(m_progressId, 0, 0, 0, tr("Scanning: %1 file(s) found").arg(partial.size()),
                                                     QString(), QString());
        });
}

void TabMediaViewerViewModel::onCandidatesReady(int generation, Result<std::vector<FileNode>> result)
{
    if (generation != m_generation)
    {
        // Superseded by a newer request, or exit()/the tab was closed -- discard.
        return;
    }

    if (result.hasError())
    {
        if (m_progressId != 0)
        {
            m_notificationCenter.terminateProgressNotification(m_progressId);
            m_progressId = 0;
        }
        emit failed(QString::fromStdString(result.error().message));
        emit loadingFinished();
        return;
    }

    m_candidates = std::move(result).value();
    // Fresh candidate set -- none of the three kinds has had its background sweep dispatched yet
    // (Architecture.md §14.66/§14.80).
    m_imageSweepRequested = false;
    m_videoSweepRequested = false;
    m_animationSweepRequested = false;

    // Fresh candidate set from a new recursive/tag-first scan (possibly a different root than
    // whatever this tab's media viewer last showed) -- unlike a plain kind/name re-filter of
    // candidates already held, stale cached icons/thumbnails from the previous set are not worth
    // keeping around, so let applyFilter() clear them via setEntries().
    applyFilter(true);
    // Only the currently-selected kind is decoded eagerly (Architecture.md §14.66) -- the other
    // kind, if any, is only decoded if/when the dropdown switches to it (setKind).
    requestKindSweep(generation, m_kind);

    emit loadingFinished();
}

std::vector<FileNode> TabMediaViewerViewModel::filterCandidates() const
{
    std::vector<FileNode> filtered;
    filtered.reserve(m_candidates.size());
    for (const FileNode& candidate : m_candidates)
    {
        // Scoped to currentFolder() rather than m_root (Architecture.md §14.74): equal to m_root
        // whenever Folder mode hasn't navigated anywhere, so this is a no-op for the common case,
        // but lets switching from ByFolder back to Recursive show a recursive gallery of wherever
        // Folder-mode browsing currently is instead of snapping back out to the full original
        // root.
        if (!isPathWithinOrEqual(candidate.path(), m_currentFolder))
        {
            continue;
        }

        const std::string extension = MediaExtensions::lowercaseExtension(candidate.path());
        if (mediaViewerKindMatches(extension, m_kind))
        {
            filtered.push_back(candidate);
        }
    }

    return FileNavigationUseCase::filterByName(std::move(filtered), m_nameQuery.toStdString(), m_nameOptions);
}

std::vector<FileNode> TabMediaViewerViewModel::sortResults(std::vector<FileNode> filtered) const
{
    switch (m_sortCriterion)
    {
    case MediaViewerSortCriterion::Name:
        return FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, m_sortAscending);
    case MediaViewerSortCriterion::Size:
        return FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Size, m_sortAscending);
    case MediaViewerSortCriterion::Duration:
        std::stable_sort(filtered.begin(), filtered.end(), [this](const FileNode& a, const FileNode& b) {
            const auto da = m_resultsModel->mediaThumbnailDuration(QString::fromStdWString(a.path().wstring()));
            const auto db = m_resultsModel->mediaThumbnailDuration(QString::fromStdWString(b.path().wstring()));
            if (da.has_value() != db.has_value())
            {
                // Unknown duration always sorts last, regardless of ascending/descending.
                return da.has_value();
            }
            if (!da.has_value())
            {
                return false;
            }
            return m_sortAscending ? *da < *db : *da > *db;
        });
        return filtered;
    }

    return filtered;
}

TabMediaViewerViewModel::FolderGroupedView TabMediaViewerViewModel::buildFolderGroupedView(const std::vector<FileNode>& filtered) const
{
    FolderGroupedView view;

    // First pass: partition `filtered` into direct children of m_currentFolder (kept as-is) and
    // everything else still under it, bucketed by immediate child-of-current-folder ancestor
    // directory.
    std::vector<std::filesystem::path> bucketOrder; // first-seen order, for deterministic iteration
    std::unordered_map<std::filesystem::path, std::vector<FileNode>, PathHash> buckets;

    for (const FileNode& candidate : filtered)
    {
        if (!isPathWithinOrEqual(candidate.path(), m_currentFolder))
        {
            continue;
        }

        const std::filesystem::path parent = candidate.path().parent_path();
        if (pathsEqualCaseInsensitive(parent, m_currentFolder))
        {
            view.combined.push_back(candidate);
            view.directChildFiles.push_back(candidate);
            continue;
        }

        // Climb from the candidate's parent until reaching the ancestor directory whose own
        // parent is m_currentFolder -- that directory is the bucket key.
        std::filesystem::path bucketKey = parent;
        for (;;)
        {
            const std::filesystem::path bucketParent = bucketKey.parent_path();
            if (pathsEqualCaseInsensitive(bucketParent, m_currentFolder) || bucketParent == bucketKey)
            {
                break;
            }
            bucketKey = bucketParent;
        }

        auto [it, inserted] = buckets.try_emplace(bucketKey);
        if (inserted)
        {
            bucketOrder.push_back(bucketKey);
        }
        it->second.push_back(candidate);
    }

    // Second pass: synthesize one directory FileNode + preview seed per non-empty bucket,
    // sampling from each bucket's own entries in the current sort order (Architecture.md §14.74).
    for (const std::filesystem::path& bucketKey : bucketOrder)
    {
        auto node = FileNode::create(bucketKey, 0, std::chrono::system_clock::now(), std::chrono::system_clock::now(),
                                      FileType::Directory);
        if (!node)
        {
            continue;
        }

        const std::vector<FileNode> bucketEntries = sortResults(buckets.at(bucketKey));

        FolderPreviewSeed seed;
        seed.itemCount = static_cast<int>(bucketEntries.size());
        const std::size_t sampleCount = std::min<std::size_t>(4, bucketEntries.size());
        seed.samplePaths.reserve(sampleCount);
        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            seed.samplePaths.push_back(QString::fromStdWString(bucketEntries[i].path().wstring()));
            view.previewSampleFiles.push_back(bucketEntries[i]);
        }

        view.previewsByPath.insert(QString::fromStdWString(bucketKey.wstring()), seed);
        view.combined.push_back(node.value());
    }

    return view;
}

TabMediaViewerViewModel::Display TabMediaViewerViewModel::buildDisplay() const
{
    Display display;
    std::vector<FileNode> kindNameFiltered = filterCandidates();

    display.grouped = m_groupMode == MediaViewerGroupMode::ByFolder && m_viewMode == MediaViewerViewMode::Thumbnails;

    if (!display.grouped)
    {
        display.entries = sortResults(std::move(kindNameFiltered));
        display.dispatchTargets = display.entries;
        return display;
    }

    FolderGroupedView grouped = buildFolderGroupedView(kindNameFiltered);
    display.entries = sortResults(std::move(grouped.combined));
    display.dispatchTargets = std::move(grouped.directChildFiles);
    // Folder tiles' collage samples need their own icon/thumbnail/tag request too (Architecture.md
    // §14.74) -- otherwise they'd only ever fill in once requestKindSweep's whole-tree Low-priority
    // background sweep happens to reach them, which can take a long time in a large tree even
    // though only a handful of samples are actually on screen right now.
    display.dispatchTargets.insert(display.dispatchTargets.end(), grouped.previewSampleFiles.begin(), grouped.previewSampleFiles.end());
    display.folderPreviews = std::move(grouped.previewsByPath);
    return display;
}

void TabMediaViewerViewModel::reapplySort()
{
    if (!m_active)
    {
        return;
    }

    const Display display = buildDisplay();
    m_resultsModel->refilterEntries(display.entries, /*preSorted=*/true);
    if (display.grouped)
    {
        m_resultsModel->setFolderPreviews(display.folderPreviews);
    }
}

void TabMediaViewerViewModel::applyFilter(bool freshCandidates)
{
    const Display display = buildDisplay();

    if (freshCandidates)
    {
        m_resultsModel->setEntriesPreSorted(display.grouped ? m_currentFolder : m_root, display.entries);
    }
    else
    {
        // Image<->Video kind toggle, a name-query keystroke, or a Folder-mode regroup: narrowing/
        // regrouping the SAME already-fetched candidate set, not a new one, so this goes through
        // refilterEntries() instead of setEntries() -- it leaves the model's icon/thumbnail caches
        // alone, so a tile that drops out of view and later comes back (toggling the kind filter
        // back, say) stays showing its already-decoded thumbnail instead of flashing back to the
        // placeholder icon while MediaThumbnailLoadingService redundantly redecodes something it
        // already decoded (Architecture.md §14.62).
        m_resultsModel->refilterEntries(display.entries, /*preSorted=*/true);
    }

    if (display.grouped)
    {
        m_resultsModel->setFolderPreviews(display.folderPreviews);
    }

    // Reuses the current generation rather than bumping it: this is a re-filter/fresh-listing
    // handled above, not itself a new fetch, so in-flight replies for this generation are still
    // valid. Still needed even when refilterEntries() preserved the cache: entries that are newly
    // visible after narrowing/widening the filter may not have been requested before. Scoped to
    // display.dispatchTargets rather than display.entries when grouped (Architecture.md §14.74):
    // synthesized subfolder rows never need their own icon/thumbnail/tag request, since their
    // collage is drawn entirely from already-requested sample children.
    requestResultIcons(m_generation, display.dispatchTargets);
    // High priority (Architecture.md §14.66): `display.dispatchTargets` is the kind+name match on
    // screen right now, so it jumps ahead of a kind's background sweep that may already be mid-
    // flight. Its completions still count toward the active progress notification (de-duplicated by
    // path against the sweep's own total) -- see requestResultThumbnails's doc comment.
    requestResultThumbnails(m_generation, display.dispatchTargets, MediaThumbnailLoadingService::Priority::High);
    // Details view's tag row (Architecture.md §14.69) -- harmless to fetch even while another
    // view mode is active, mirroring requestResultIcons/Thumbnails above.
    requestResultTags(m_generation, display.dispatchTargets);
    emit resultsChanged(display.entries);
}

void TabMediaViewerViewModel::requestKindSweep(int generation, MediaViewerKind kind)
{
    bool* sweepRequested = &m_imageSweepRequested;
    if (kind == MediaViewerKind::Video)
    {
        sweepRequested = &m_videoSweepRequested;
    }
    else if (kind == MediaViewerKind::Animation)
    {
        sweepRequested = &m_animationSweepRequested;
    }
    if (*sweepRequested)
    {
        return;
    }
    *sweepRequested = true;

    // Ignores the name filter -- this is the whole-kind background sweep, so clearing the name
    // filter later never has to wait on a fresh decode pass.
    std::vector<FileNode> kindCandidates;
    kindCandidates.reserve(m_candidates.size());
    for (const FileNode& candidate : m_candidates)
    {
        const std::string extension = MediaExtensions::lowercaseExtension(candidate.path());
        if (mediaViewerKindMatches(extension, kind))
        {
            kindCandidates.push_back(candidate);
        }
    }

    m_thumbnailsTotal = kindCandidates.size();
    m_thumbnailsDone = 0;
    m_countedPaths.clear();

    if (m_thumbnailsTotal == 0)
    {
        // Nothing to decode for this kind -- terminate now rather than leaving a determinate 0/0
        // bar up until the next mode change.
        if (m_progressId != 0)
        {
            m_notificationCenter.terminateProgressNotification(m_progressId);
            m_progressId = 0;
        }
    }
    else
    {
        // Reuses an already-open notification in place (the "Scanning folder..." bar run() just
        // created, for the first sweep of a fresh fetch) rather than terminating and recreating
        // it; otherwise (switching to a kind whose own sweep hasn't run yet this generation, well
        // after the fetch's own bar already closed) opens a fresh one -- this is what makes that
        // switch visibly show loading progress (Architecture.md §14.66).
        if (m_progressId == 0)
        {
            m_progressId = m_notificationCenter.createProgressNotification(tr("Loading Media Viewer..."), /*barCount=*/1,
                                                                              /*cancellable=*/true);
        }
        m_notificationCenter.updateProgressBar(m_progressId, 0, 0, static_cast<int>(m_thumbnailsTotal),
                                                 tr("Loading %1 item(s)...").arg(m_thumbnailsTotal), tr("0 of %1").arg(m_thumbnailsTotal),
                                                 QString());
    }

    // Icons for non-media files are never shown in the grid, so this (like the thumbnail request
    // right after) is scoped to kindCandidates rather than every file in the tree.
    requestResultIcons(generation, kindCandidates);
    // Low priority (Architecture.md §14.66): this is the whole-kind background sweep, so it never
    // blocks whatever currently matches the visible filter (dispatched High by applyFilter).
    requestResultThumbnails(generation, kindCandidates, MediaThumbnailLoadingService::Priority::Low);
}

std::vector<Tag> TabMediaViewerViewModel::resolveTagFilters() const
{
    auto result = m_tagManagementUseCase.allTags();
    if (result.hasError())
    {
        return {};
    }

    std::vector<Tag> resolved;
    for (const Tag& tag : result.value())
    {
        if (std::find(m_tagIds.begin(), m_tagIds.end(), tag.id()) != m_tagIds.end())
        {
            resolved.push_back(tag);
        }
    }
    return resolved;
}

void TabMediaViewerViewModel::emitFolderAvailability()
{
    emit backAvailableChanged(m_folderHistory.canGoBack());
    emit forwardAvailableChanged(m_folderHistory.canGoForward());
    emit upAvailableChanged(upTarget() != m_currentFolder);
}

void TabMediaViewerViewModel::requestResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(m_resultsModel, m_root, candidates, iconSizeMaskFor(ViewMode::ExtraLargeIcons),
                                        [this, generation](QHash<QString, QIcon> batch) {
                                            if (generation != m_generation)
                                            {
                                                // Superseded/abandoned, same discard-by-generation-id
                                                // approach onCandidatesReady uses.
                                                return;
                                            }

                                            m_resultsModel->setIcons(batch);
                                        });
}

void TabMediaViewerViewModel::requestResultThumbnails(int generation, const std::vector<FileNode>& candidates,
                                                        MediaThumbnailLoadingService::Priority priority)
{
    // 256x256 matches FileBrowserView.cpp's iconSizeFor(ViewMode::ExtraLargeIcons) -- the fixed
    // grid sizing MediaViewerGrid mode locks to (Architecture.md §14.62).
    m_mediaThumbnailLoadingService.requestThumbnails(
        m_resultsModel, m_root, candidates, 256, 256,
        [this, generation](QHash<QString, MediaThumbnailData> batch) {
            if (generation != m_generation)
            {
                // Superseded/abandoned, same discard-by-generation-id approach onCandidatesReady
                // uses.
                return;
            }

            m_resultsModel->setMediaThumbnails(batch);

            if (m_sortCriterion == MediaViewerSortCriterion::Duration)
            {
                // Re-orders the grid progressively as video durations finish decoding, instead of
                // only once (Architecture.md §14.68).
                reapplySort();
            }

            if (m_progressId != 0)
            {
                // De-duplicated by path (Architecture.md §14.66): a High-priority filter-match
                // dispatch and this kind's Low-priority background sweep can both decode the same
                // path, so raw batch-size summation would double-count it against
                // m_thumbnailsTotal. Only a path not already counted advances "done".
                for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
                {
                    if (m_countedPaths.contains(it.key()))
                    {
                        continue;
                    }
                    m_countedPaths.insert(it.key());
                    ++m_thumbnailsDone;
                }

                const std::size_t done = std::min(m_thumbnailsDone, m_thumbnailsTotal);
                m_notificationCenter.updateProgressBar(m_progressId, 0, static_cast<int>(done), static_cast<int>(m_thumbnailsTotal),
                                                         tr("Loading %1 item(s)...").arg(m_thumbnailsTotal),
                                                         tr("%1 of %2").arg(done).arg(m_thumbnailsTotal), QString());

                if (done >= m_thumbnailsTotal)
                {
                    m_notificationCenter.terminateProgressNotification(m_progressId);
                    m_progressId = 0;
                }
            }
        },
        priority);
}

void TabMediaViewerViewModel::requestResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_generation)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onCandidatesReady
            // uses.
            return;
        }

        m_resultsModel->setFileTags(result ? result.value() : TagsByPathMap{});
    });
}
