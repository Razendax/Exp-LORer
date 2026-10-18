#include "TabAdvancedSearchViewModel.h"

#include <algorithm>
#include <utility>

#include "AdvancedSearchService.h"
#include "FileNavigationUseCase.h"
#include "FileTagsService.h"
#include "IconLoadingService.h"
#include "TagChangeNotifier.h"
#include "TagManagementUseCase.h"

namespace
{
    bool isCriteriaEmpty(const SearchCriteria& criteria)
    {
        return criteria.nameQuery.empty() && !criteria.minSizeBytes && !criteria.maxSizeBytes && criteria.tagIds.empty();
    }
}

TabAdvancedSearchViewModel::TabAdvancedSearchViewModel(FileNavigationUseCase& fileNavigationUseCase,
                                                         AdvancedSearchService& advancedSearchService,
                                                         TagManagementUseCase& tagManagementUseCase, FileTagsService& fileTagsService,
                                                         IconLoadingService& iconLoadingService, TagChangeNotifier& tagChangeNotifier,
                                                         const FileDecorationRules& fileDecorationRules,
                                                         QObject& fileDecorationsChangeSource, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_advancedSearchService(advancedSearchService)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileTagsService(fileTagsService)
    , m_iconLoadingService(iconLoadingService)
    , m_resultsModel(new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this))
{
    connect(&tagChangeNotifier, &TagChangeNotifier::tagsChanged, this, [this]() {
        if (!m_active)
        {
            return;
        }

        requestResultTags(m_generation, m_fullScanDone ? m_snapshot : m_partialSnapshot);
    });
}

std::vector<Tag> TabAdvancedSearchViewModel::resolveTagCriteria() const
{
    auto result = m_tagManagementUseCase.allTags();
    if (result.hasError())
    {
        return {};
    }

    std::vector<Tag> resolved;
    for (const Tag& tag : result.value())
    {
        if (std::find(m_criteria.tagIds.begin(), m_criteria.tagIds.end(), tag.id()) != m_criteria.tagIds.end())
        {
            resolved.push_back(tag);
        }
    }
    return resolved;
}

void TabAdvancedSearchViewModel::showPanel(const std::filesystem::path& currentPath)
{
    if (m_active)
    {
        return;
    }

    showCurrentFolderNonRecursive(currentPath);

    m_active = true;
    emit modeChanged(true);
}

void TabAdvancedSearchViewModel::showCurrentFolderNonRecursive(const std::filesystem::path& currentPath)
{
    // Not a real (recursive) search -- just a non-recursive listing of the current folder.
    auto result = m_fileNavigationUseCase.listDirectory(currentPath);
    if (result.hasError())
    {
        emit failed(QString::fromStdString(result.error().message));
        return;
    }

    // Discards any in-flight/cached scan -- mirrors exit()'s cleanup, but m_active/m_criteria are
    // left untouched by this function (the pane stays open, and the criteria that led here, e.g.
    // all-empty fields, is still whatever the caller already set).
    m_snapshot.clear();
    m_partialSnapshot.clear();
    m_snapshotTags.clear();
    m_snapshotIcons.clear();
    m_root.clear();
    m_cachedTagIds.clear();
    m_cachedTagIncludeChildren = false;
    m_fullScanDone = false;
    m_scanPending = false;
    ++m_generation;

    auto sorted = FileNavigationUseCase::sortBy(std::move(result).value(), SortCriterion::Name, true);
    m_resultsModel->setEntries(currentPath, sorted);
    requestResultTags(m_generation, sorted);
    requestResultIcons(m_generation, sorted);

    emit resultsChanged(sorted);
    emit criteriaChanged(m_criteria);
}

void TabAdvancedSearchViewModel::start(const SearchCriteria& criteria, const std::filesystem::path& currentPath)
{
    m_criteria = criteria;

    if (isCriteriaEmpty(criteria))
    {
        if (!m_active)
        {
            m_active = true;
            emit modeChanged(true);
        }
        showCurrentFolderNonRecursive(currentPath);
        return;
    }

    run(/*forceRescan=*/true, currentPath);
}

void TabAdvancedSearchViewModel::updateCriteria(const SearchCriteria& criteria, const std::filesystem::path& currentPath)
{
    if (!m_active)
    {
        return;
    }

    const bool skipGitIgnoredChanged = criteria.nameOptions.skipGitIgnored != m_criteria.nameOptions.skipGitIgnored;
    const bool tagIncludeChildTagsChanged = criteria.tagIncludeChildTags != m_criteria.tagIncludeChildTags;

    m_criteria = criteria;

    if (isCriteriaEmpty(criteria))
    {
        showCurrentFolderNonRecursive(currentPath);
        return;
    }

    run(/*forceRescan=*/skipGitIgnoredChanged || tagIncludeChildTagsChanged, currentPath);
}

void TabAdvancedSearchViewModel::exit()
{
    if (!m_active)
    {
        return;
    }

    m_active = false;
    m_snapshot.clear();
    m_partialSnapshot.clear();
    m_snapshotTags.clear();
    m_snapshotIcons.clear();
    m_criteria = SearchCriteria();
    m_root.clear();
    m_cachedTagIds.clear();
    m_cachedTagIncludeChildren = false;
    m_fullScanDone = false;
    m_scanPending = false;
    // Invalidates any scan/stat-loop still in flight -- its (now-stale) result is discarded by
    // onCandidatesReady/onProgress's generation check when it eventually arrives.
    ++m_generation;

    emit modeChanged(false);
}

void TabAdvancedSearchViewModel::addTagCriterion(Tag::Id tagId, const std::filesystem::path& currentPath)
{
    if (std::find(m_criteria.tagIds.begin(), m_criteria.tagIds.end(), tagId) != m_criteria.tagIds.end())
    {
        return;
    }

    const bool wasActive = m_active;
    if (!wasActive)
    {
        m_active = true;
        emit modeChanged(true);
    }

    m_criteria.tagIds.push_back(tagId);
    run(/*forceRescan=*/!wasActive, currentPath);
}

void TabAdvancedSearchViewModel::removeTagCriterion(Tag::Id tagId, const std::filesystem::path& currentPath)
{
    auto& tagIds = m_criteria.tagIds;
    const auto it = std::find(tagIds.begin(), tagIds.end(), tagId);
    if (it == tagIds.end())
    {
        return;
    }

    tagIds.erase(it);

    if (m_active)
    {
        if (isCriteriaEmpty(m_criteria))
        {
            showCurrentFolderNonRecursive(currentPath);
        }
        else
        {
            run(/*forceRescan=*/false, currentPath);
        }
    }
}

void TabAdvancedSearchViewModel::run(bool forceRescan, const std::filesystem::path& currentPath)
{
    if (!m_criteria.tagIds.empty())
    {
        // Tag-first path (Architecture.md §14.22): the tag DB alone answers "which files have this
        // tag" -- no recursive folder scan needed. m_root is still set (for scoping + setEntries's
        // root arg).
        const std::vector<Tag::Id>& tagIds = m_criteria.tagIds;
        const bool includeChildren = m_criteria.tagIncludeChildTags;

        if (!forceRescan && m_cachedTagIds == tagIds && m_cachedTagIncludeChildren == includeChildren &&
            (m_fullScanDone || m_scanPending))
        {
            // Same tag set already fetched, or a fetch for it is already in flight (the user kept
            // typing/adjusting the name/size fields while it runs) -- don't re-dispatch a redundant
            // DB query + per-file stat loop. Just re-filter whatever candidates are available now;
            // an in-flight fetch's own completion callback will apply the latest criteria once it
            // arrives, same "reuse the in-flight request" posture as the non-tag branch below.
            applyFilter(m_fullScanDone ? m_snapshot : m_partialSnapshot);
            return;
        }

        m_root = currentPath;
        m_cachedTagIds = tagIds;
        m_cachedTagIncludeChildren = includeChildren;
        m_fullScanDone = false;
        m_scanPending = true;
        m_partialSnapshot.clear();
        m_snapshotIcons.clear();

        const int generation = ++m_generation;

        emit loading();
        auto handleCandidatesReady = [this, generation](Result<std::vector<FileNode>> result) {
            onCandidatesReady(generation, std::move(result), /*isNewSnapshot=*/true);
        };
        if (includeChildren)
        {
            m_advancedSearchService.requestTagFirstCandidatesIncludingChildren(this, m_root, tagIds, std::move(handleCandidatesReady));
        }
        else
        {
            m_advancedSearchService.requestTagFirstCandidates(this, m_root, tagIds, std::move(handleCandidatesReady));
        }
        return;
    }

    // m_cachedTagIds non-empty here means whatever m_snapshot/m_fullScanDone/m_scanPending
    // currently hold pertains to the tag-first branch above (a tag-restricted candidate set), not
    // a real recursive scan of m_root -- reusing it here (now that tagIds is empty) would silently
    // omit every untagged file. Treat it the same as "no scan cached yet" so a fresh full scan is
    // dispatched.
    const bool cachedResultIsTagFirst = !m_cachedTagIds.empty();

    if (forceRescan || m_root.empty() || cachedResultIsTagFirst || (!m_fullScanDone && !m_scanPending))
    {
        // No scan covering the current root is running or cached -- dispatch a fresh one. This also
        // covers forceRescan explicitly re-scanning a root that *was* already fully/partially
        // scanned (e.g. Search clicked again, or the criteria switched away from tags and back).
        m_root = currentPath;
        // m_snapshot/m_fullScanDone/m_scanPending are about to be overwritten with this recursive
        // scan's own results -- invalidate the tag-first cache-key above so it can't later be
        // mistaken for a still-valid tag-first candidate cache.
        m_cachedTagIds.clear();
        m_cachedTagIncludeChildren = false;
        m_fullScanDone = false;
        m_partialSnapshot.clear();
        m_snapshotIcons.clear();
        m_scanPending = true;

        const int generation = ++m_generation;

        emit loading();
        m_advancedSearchService.requestRecursiveScan(
            this, m_root,
            [this, generation](Result<std::vector<FileNode>> result) {
                onCandidatesReady(generation, std::move(result), /*isNewSnapshot=*/true);
            },
            [this, generation](std::vector<FileNode> partial) { onProgress(generation, std::move(partial)); },
            m_criteria.nameOptions.skipGitIgnored);
        return;
    }

    if (!m_fullScanDone && m_scanPending)
    {
        // A recursive scan of this same root is already running (the user kept typing/adjusting
        // criteria while it's in flight) -- don't start a second, redundant walk of the same
        // subtree. Just re-filter whatever the in-flight scan has reported so far; its own next
        // progress tick (or final result) will pick up m_criteria as it stands then, so the latest
        // criteria is never lost (Architecture.md §14.37).
        applyFilter(m_partialSnapshot);
        return;
    }

    // Cached, no rescan needed -- no disk I/O, so this stays fully synchronous (the common "user
    // is typing in the Name field" case).
    applyFilter(m_snapshot);
}

void TabAdvancedSearchViewModel::applyFilter(const std::vector<FileNode>& candidates)
{
    auto filtered = FileNavigationUseCase::filterByCriteria(candidates, m_criteria);
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_resultsModel->setEntries(m_root, filtered);
    // setEntries() above clears the model's tags -- m_snapshotTags already has every tag for the
    // current candidate list (requestResultTags fetched it whenever that list itself last
    // changed), so re-applying it here needs no new DB round trip even though this runs on every
    // criteria edit.
    m_resultsModel->setFileTags(m_snapshotTags);
    // setEntries() also clears the model's own icon cache -- same reapply, from m_snapshotIcons,
    // so rows whose icon IconLoadingService already resolved don't flash back to the placeholder
    // on every criteria edit (see TabQuickSearchViewModel's m_snapshotIcons for the mirror of
    // this).
    m_resultsModel->setIcons(m_snapshotIcons);

    if (!m_active)
    {
        m_active = true;
        emit modeChanged(true);
    }

    emit resultsChanged(filtered);
    emit criteriaChanged(m_criteria);
}

void TabAdvancedSearchViewModel::onCandidatesReady(int generation, Result<std::vector<FileNode>> result, bool isNewSnapshot)
{
    if (generation != m_generation)
    {
        // Superseded by a newer request, or the search/tab was closed in the meantime -- discard.
        return;
    }

    if (isNewSnapshot)
    {
        m_scanPending = false;
    }

    if (result.hasError())
    {
        emit failed(QString::fromStdString(result.error().message));
        return;
    }

    if (isNewSnapshot)
    {
        m_snapshot = std::move(result).value();
        m_partialSnapshot.clear();
        m_fullScanDone = true;
        applyFilter(m_snapshot);
        requestResultTags(generation, m_snapshot);
        requestResultIcons(generation, m_snapshot);
    }
    else
    {
        applyFilter(result.value());
    }

    emit loadingFinished();
}

void TabAdvancedSearchViewModel::onProgress(int generation, std::vector<FileNode> partial)
{
    if (generation != m_generation)
    {
        // Superseded/abandoned, same as onCandidatesReady -- discard.
        return;
    }

    m_partialSnapshot = std::move(partial);
    applyFilter(m_partialSnapshot);
    requestResultTags(generation, m_partialSnapshot);
    requestResultIcons(generation, m_partialSnapshot);
}

void TabAdvancedSearchViewModel::setIconMask(IconSizeMask mask)
{
    const bool changed = mask != m_iconMask;
    m_iconMask = mask;

    if (changed && m_active)
    {
        requestResultIcons(m_generation, m_resultsModel->entries());
    }
}

void TabAdvancedSearchViewModel::requestResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(m_resultsModel, m_root, candidates, m_iconMask, [this, generation](QHash<QString, QIcon> batch) {
        if (generation != m_generation)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onCandidatesReady/
            // onProgress use.
            return;
        }

        // Accumulated into m_snapshotIcons -- see TabQuickSearchViewModel::requestResultIcons/
        // m_snapshotIcons for why.
        for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
        {
            m_snapshotIcons.insert(it.key(), it.value());
        }
        m_resultsModel->setIcons(batch);
    });
}

void TabAdvancedSearchViewModel::requestResultTags(int generation, const std::vector<FileNode>& candidates)
{
    m_fileTagsService.requestTags(this, candidates, [this, generation](Result<TagsByPathMap> result) {
        if (generation != m_generation)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onCandidatesReady/
            // onProgress use.
            return;
        }

        m_snapshotTags = result ? result.value() : TagsByPathMap{};
        m_resultsModel->setFileTags(m_snapshotTags);
    });
}
