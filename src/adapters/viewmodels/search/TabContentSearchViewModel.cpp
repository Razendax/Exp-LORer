#include "TabContentSearchViewModel.h"

#include <utility>

#include "ContentSearchResultsModel.h"
#include "ContentSearchService.h"
#include "ContentSearchUseCase.h"
#include "FileListModel.h"

TabContentSearchViewModel::TabContentSearchViewModel(ContentSearchService& contentSearchService, QObject* parent)
    : QObject(parent)
    , m_contentSearchService(contentSearchService)
    , m_resultsModel(new ContentSearchResultsModel(this))
{
}

void TabContentSearchViewModel::updateQuery(const QString& query, const std::filesystem::path& root)
{
    run(query, root);
}

void TabContentSearchViewModel::refresh(const QString& query, const std::filesystem::path& root)
{
    run(query, root);
}

void TabContentSearchViewModel::run(const QString& query, const std::filesystem::path& root)
{
    if (query.trimmed().isEmpty())
    {
        exit();
        return;
    }

    m_query = query;
    dispatchScan(root);
}

void TabContentSearchViewModel::dispatchScan(const std::filesystem::path& root)
{
    m_root = root;
    const int generation = ++m_generation;

    emit loading();

    // Hidden content is excluded from the scan itself (not just from display) per spec, so this
    // reads the app-wide "Show hidden files/folders" setting directly at dispatch time rather than
    // being threaded in as a parameter -- there is no per-instance model-level visibility flip for
    // content search the way FileListModel/quick-search/advanced-search results models each have,
    // since this ViewModel has no display-only filtering step to flip.
    const bool showHidden = FileListModel::showHiddenFilesEnabled();

    // onProgress is intentionally not wired here: ContentSearchUseCase::collectSearchableContent
    // forwards its progress ticks straight from FileNavigationUseCase::listDirectoryRecursive's own
    // walk, which fires with the raw FileNodes discovered so far -- before any of them have had
    // their content read/filtered. There is no partial RawFileLines data available at tick time to
    // show progressively, unlike AdvancedSearchWorker's candidates (which a progress tick already
    // in its final form).
    m_contentSearchService.requestScan(
        this, root, m_options.includePattern, m_options.includeOptions, m_options.excludePattern, m_options.excludeOptions,
        m_options.skipGitIgnored, showHidden,
        [this, generation](Result<std::vector<RawFileLines>> result) { onScanReady(generation, std::move(result)); });
}

void TabContentSearchViewModel::onScanReady(int generation, Result<std::vector<RawFileLines>> result)
{
    if (generation != m_generation)
    {
        // Superseded by a newer dispatch, or the tab was closed/content search exited in the
        // meantime -- discard.
        return;
    }

    if (result.hasError())
    {
        emit failed(QString::fromStdString(result.error().message));
        emit loadingFinished();
        return;
    }

    m_lastScanFiles = std::move(result).value();
    applyFilter();

    emit loadingFinished();
}

void TabContentSearchViewModel::applyFilter()
{
    m_lastMatches = ContentSearchUseCase::filterLines(m_lastScanFiles, m_query.toStdString(), m_options.queryOptions);
    m_resultsModel->setResults(m_lastMatches, m_root);

    if (!m_active)
    {
        m_active = true;
        emit modeChanged(true);
    }

    emit resultsChanged(m_lastMatches);
}

void TabContentSearchViewModel::setOptions(const ContentSearchOptions& options, const std::filesystem::path& root)
{
    const bool scanAffectingChanged = options.skipGitIgnored != m_options.skipGitIgnored ||
                                       options.includePattern != m_options.includePattern ||
                                       options.includeOptions.mode != m_options.includeOptions.mode ||
                                       options.includeOptions.caseSensitive != m_options.includeOptions.caseSensitive ||
                                       options.excludePattern != m_options.excludePattern ||
                                       options.excludeOptions.mode != m_options.excludeOptions.mode ||
                                       options.excludeOptions.caseSensitive != m_options.excludeOptions.caseSensitive;
    const bool queryOptionsChanged = options.queryOptions.mode != m_options.queryOptions.mode ||
                                      options.queryOptions.caseSensitive != m_options.queryOptions.caseSensitive;

    m_options = options;

    if (m_query.isEmpty())
    {
        // Nothing to filter/scan yet -- the new options still take effect the next time a query is
        // typed.
        return;
    }

    if (scanAffectingChanged)
    {
        // Changes what gets *scanned*, not just how an already-scanned candidate list is filtered
        // -- same orthogonality ContentSearchOptions::skipGitIgnored's own comment documents. Takes
        // priority over a simultaneous queryOptions change: the fresh scan's own completion applies
        // m_query with whatever m_options.queryOptions already holds by then.
        dispatchScan(root);
        return;
    }

    if (queryOptionsChanged && !m_lastScanFiles.empty())
    {
        applyFilter();
    }
}

void TabContentSearchViewModel::exit()
{
    if (!m_active)
    {
        return;
    }

    m_active = false;
    m_query.clear();
    m_options = ContentSearchOptions{};
    m_lastScanFiles.clear();
    m_lastMatches.clear();
    m_resultsModel->setResults({}, m_root);
    m_root.clear();
    ++m_generation;

    emit modeChanged(false);
}
