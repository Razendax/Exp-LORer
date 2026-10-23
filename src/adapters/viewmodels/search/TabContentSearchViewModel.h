#pragma once

#include <filesystem>
#include <vector>

#include <QObject>
#include <QString>

#include "ContentSearchMatch.h"
#include "ContentSearchOptions.h"
#include "Result.h"

class ContentSearchResultsModel;
class ContentSearchService;

// "Find in Files" state for a single tab (Architecture.md §14.104). Owned by TabViewModel via
// `this`-parenting, exposed through TabViewModel::contentSearch(); callers call its slots and
// connect to its signals directly, same posture as TabQuickSearchViewModel.
//
// Deliberately simpler than TabQuickSearchViewModel: there is no "scan once, re-filter forever"
// shortcut here -- per spec, every settled query-text edit is a fresh disk+content scan (content
// search is "heavier" than filename search). The only in-memory-only re-filter path is toggling
// Match Case/Whole Word/Regex on the main query with the query text itself unchanged.
class TabContentSearchViewModel : public QObject
{
    Q_OBJECT

public:
    explicit TabContentSearchViewModel(ContentSearchService& contentSearchService, QObject* parent = nullptr);

    ContentSearchResultsModel* resultsModel() const noexcept { return m_resultsModel; }
    bool isActive() const noexcept { return m_active; }
    QString query() const noexcept { return m_query; }
    // The most recent scan's root -- ContentSearchResultsModel itself exposes no such accessor
    // (unlike FileListModel::currentDirectory()), since it only ever needs root as a one-shot
    // setResults() parameter for computing each header row's path-disambiguation text, not as
    // ongoing state. MainWindow needs it anyway, to re-hand it to ContentSearchPanelWidget's own
    // (separate) results model on every resultsChanged -- same "widget owns its own copy" posture
    // SearchPanelWidget's m_listModel/m_treeModel already have relative to TabQuickSearchViewModel.
    const std::filesystem::path& root() const noexcept { return m_root; }
    ContentSearchOptions options() const noexcept { return m_options; }
    // The exact matches last pushed into m_resultsModel/resultsChanged -- MainWindow needs the raw
    // vector (not just the model) to re-seed ContentSearchPanelWidget's own results model when a
    // tab regains focus, same reason TabQuickSearchViewModel::resultsModel() exposes entries().
    const std::vector<FileContentMatch>& matches() const noexcept { return m_lastMatches; }

public slots:
    // Empty/whitespace-only query exits (no-op if already inactive). Otherwise always dispatches a
    // fresh recursive scan+content read of `root` -- never served from a cached snapshot, unlike
    // TabQuickSearchViewModel::updateQuery.
    void updateQuery(const QString& query, const std::filesystem::path& root);

    // Same dispatch semantics as updateQuery (every non-empty query already always rescans here) --
    // kept as its own entry point to mirror TabQuickSearchViewModel's updateQuery/refresh split
    // (Refresh button / Enter).
    void refresh(const QString& query, const std::filesystem::path& root);

    // Updates the toggles/Include/Exclude fields. If only queryOptions (Match Case/Whole
    // Word/Regex on the main query) differs, re-filters m_lastScanFiles synchronously, no dispatch
    // (a no-op beyond caching the new options if nothing has been scanned yet). If
    // skipGitIgnored/includePattern/includeOptions/excludePattern/excludeOptions differs, forces a
    // fresh scan dispatch instead -- those change what gets *read*, not just what's displayed. A
    // no-op beyond caching the new options if the query is currently empty.
    void setOptions(const ContentSearchOptions& options, const std::filesystem::path& root);

    // Clears state and flips isActive() off. No-op if already inactive.
    void exit();

signals:
    void modeChanged(bool active);
    void resultsChanged(const std::vector<FileContentMatch>& matches);
    void failed(const QString& message);
    // Emitted right before dispatching a scan request.
    void loading();
    // Emitted once the in-flight scan that most recently triggered loading() actually finishes
    // (successfully -- failed covers the error case).
    void loadingFinished();

private:
    // Shared body of updateQuery/refresh.
    void run(const QString& query, const std::filesystem::path& root);

    // Dispatches a fresh ContentSearchService::requestScan against `root` using the currently
    // cached m_options/m_query, bumping generation.
    void dispatchScan(const std::filesystem::path& root);

    // Applies ContentSearchUseCase::filterLines(m_lastScanFiles, m_query, m_options.queryOptions),
    // pushes the result into m_resultsModel, activates if needed, and emits resultsChanged.
    void applyFilter();

    // Completion callback for dispatchScan's requestScan. Discards the result if `generation` no
    // longer matches m_generation (a newer dispatch, or exit()/tab close, superseded it).
    void onScanReady(int generation, Result<std::vector<RawFileLines>> result);

    ContentSearchService& m_contentSearchService;

    ContentSearchResultsModel* m_resultsModel = nullptr;
    bool m_active = false;
    std::filesystem::path m_root;
    QString m_query;
    ContentSearchOptions m_options;
    // The most recent scan's structurally-filtered-but-not-yet-query-filtered content -- exactly
    // what toggling Match Case/Whole Word/Regex re-filters via ContentSearchUseCase::filterLines
    // without touching disk.
    std::vector<RawFileLines> m_lastScanFiles;
    // Bumped on every scan dispatch and on exit(); onScanReady discards any reply whose generation
    // no longer matches.
    int m_generation = 0;
    // The exact matches last pushed into m_resultsModel -- see matches() above.
    std::vector<FileContentMatch> m_lastMatches;
};
