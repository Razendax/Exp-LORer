#include "TabQuickSearchViewModel.h"

#include <utility>

#include "AdvancedSearchService.h"
#include "FileNavigationUseCase.h"
#include "FileTagsService.h"
#include "IconLoadingService.h"
#include "TagChangeNotifier.h"

TabQuickSearchViewModel::TabQuickSearchViewModel(AdvancedSearchService& advancedSearchService, FileTagsService& fileTagsService,
                                                   IconLoadingService& iconLoadingService, TagChangeNotifier& tagChangeNotifier,
                                                   const FileDecorationRules& fileDecorationRules,
                                                   QObject& fileDecorationsChangeSource, QObject* parent)
    : QObject(parent)
    , m_advancedSearchService(advancedSearchService)
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

void TabQuickSearchViewModel::updateQuery(const QString& query, const std::filesystem::path& currentPath)
{
    if (query.trimmed().isEmpty())
    {
        exit();
        return;
    }

    m_query = query;
    run(currentPath, /*forceRescan=*/false);
}

void TabQuickSearchViewModel::refresh(const QString& query, const std::filesystem::path& currentPath)
{
    if (query.trimmed().isEmpty())
    {
        exit();
        return;
    }

    m_query = query;
    run(currentPath, /*forceRescan=*/true);
}

void TabQuickSearchViewModel::run(const std::filesystem::path& currentPath, bool forceRescan)
{
    if (forceRescan || m_root.empty() || (!m_fullScanDone && !m_scanPending))
    {
        // No scan covering the current root is running or cached -- dispatch a fresh one. This also
        // covers forceRescan explicitly re-scanning a root that *was* already fully/partially
        // scanned (Refresh clicked again).
        m_root = currentPath;
        m_fullScanDone = false;
        m_partialSnapshot.clear();
        m_snapshotIcons.clear();
        m_scanPending = true;

        const int generation = ++m_generation;

        emit loading();
        m_advancedSearchService.requestRecursiveScan(
            this, m_root,
            [this, generation](Result<std::vector<FileNode>> result) { onCandidatesReady(generation, std::move(result)); },
            [this, generation](std::vector<FileNode> partial) { onProgress(generation, std::move(partial)); }, m_skipGitIgnored);
        return;
    }

    if (!m_fullScanDone && m_scanPending)
    {
        // A recursive scan of this same root is already running (the user kept typing while it's
        // in flight) -- don't start a second, redundant walk of the same subtree. Just re-filter
        // whatever the in-flight scan has reported so far; its own next progress tick (or final
        // result) will pick up m_query as it stands then (Architecture.md §14.37).
        applyFilter(m_partialSnapshot);
        return;
    }

    // Cached, no rescan needed -- no disk I/O, so this stays fully synchronous (the common "user is
    // typing" case).
    applyFilter(m_snapshot);
}

void TabQuickSearchViewModel::applyFilter(const std::vector<FileNode>& candidates)
{
    auto filtered = FileNavigationUseCase::filterByName(candidates, m_query.toStdString(), options());
    filtered = FileNavigationUseCase::sortBy(std::move(filtered), SortCriterion::Name, true);
    m_resultsModel->setEntries(m_root, filtered);
    // setEntries() above clears the model's tags -- m_snapshotTags already has every tag for the
    // current candidate list (requestResultTags fetched it whenever that list itself last
    // changed), so re-applying it here needs no new DB round trip even though this runs on every
    // keystroke.
    m_resultsModel->setFileTags(m_snapshotTags);
    // setEntries() also clears the model's own icon cache -- same reapply, from m_snapshotIcons, so
    // rows whose icon IconLoadingService already resolved don't flash back to the placeholder on
    // every keystroke (see m_snapshotIcons's declaration).
    m_resultsModel->setIcons(m_snapshotIcons);

    if (!m_active)
    {
        m_active = true;
        emit modeChanged(true);
    }

    emit resultsChanged(filtered);
}

void TabQuickSearchViewModel::onCandidatesReady(int generation, Result<std::vector<FileNode>> result)
{
    if (generation != m_generation)
    {
        // Superseded by a newer dispatch, or the search/tab was closed in the meantime -- discard.
        return;
    }

    m_scanPending = false;

    if (result.hasError())
    {
        emit failed(QString::fromStdString(result.error().message));
        return;
    }

    m_snapshot = std::move(result).value();
    m_partialSnapshot.clear();
    m_fullScanDone = true;
    applyFilter(m_snapshot);
    requestResultTags(generation, m_snapshot);
    requestResultIcons(generation, m_snapshot);

    emit loadingFinished();
}

void TabQuickSearchViewModel::onProgress(int generation, std::vector<FileNode> partial)
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

void TabQuickSearchViewModel::setOptions(const NameSearchOptions& options, const std::filesystem::path& currentPath)
{
    const bool skipGitIgnoredChanged = options.skipGitIgnored != m_skipGitIgnored;

    m_matchMode = options.mode;
    m_caseSensitive = options.caseSensitive;
    m_skipGitIgnored = options.skipGitIgnored;

    if (m_query.isEmpty())
    {
        // Nothing to filter yet -- the new options still take effect the next time a query is
        // typed.
        return;
    }

    if (skipGitIgnoredChanged)
    {
        // Unlike mode/caseSensitive, this changes what a scan *visits*, not just how an
        // already-scanned candidate list is filtered -- a prior scan without pruning never
        // excluded anything, and a prior pruned scan never visited the excluded paths at all, so
        // neither cached snapshot can serve the other state from memory (Architecture.md §14.45).
        run(currentPath, /*forceRescan=*/true);
        return;
    }

    applyFilter(m_fullScanDone ? m_snapshot : m_partialSnapshot);
}

void TabQuickSearchViewModel::exit()
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
    m_query.clear();
    m_matchMode = NameMatchMode::Contains;
    m_caseSensitive = false;
    m_skipGitIgnored = false;
    m_root.clear();
    m_fullScanDone = false;
    m_scanPending = false;
    ++m_generation;

    emit modeChanged(false);
}

void TabQuickSearchViewModel::setIconMask(IconSizeMask mask)
{
    const bool changed = mask != m_iconMask;
    m_iconMask = mask;

    if (changed && m_active)
    {
        requestResultIcons(m_generation, m_resultsModel->entries());
    }
}

void TabQuickSearchViewModel::requestResultIcons(int generation, const std::vector<FileNode>& candidates)
{
    m_iconLoadingService.requestIcons(m_resultsModel, m_root, candidates, m_iconMask, [this, generation](QHash<QString, QIcon> batch) {
        if (generation != m_generation)
        {
            // Superseded/abandoned, same discard-by-generation-id approach onCandidatesReady/
            // onProgress use.
            return;
        }

        // Accumulated into m_snapshotIcons (not just applied to the model directly) so a later
        // applyFilter re-filter -- which clears the model's own icon cache via setEntries() -- can
        // restore every icon already resolved so far instead of flashing back to placeholders (see
        // m_snapshotIcons's declaration).
        for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
        {
            m_snapshotIcons.insert(it.key(), it.value());
        }
        m_resultsModel->setIcons(batch);
        // The Search tab's results actually render in the left panel's SearchPanelWidget, via its
        // own separate FileListModel that m_resultsModel's setIcons() above never touches --
        // MainWindow relays this to it via resultIcons().
        emit resultIconsChanged();
    });
}

void TabQuickSearchViewModel::requestResultTags(int generation, const std::vector<FileNode>& candidates)
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
