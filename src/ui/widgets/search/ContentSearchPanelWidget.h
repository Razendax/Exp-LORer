#pragma once

#include <filesystem>
#include <vector>

#include <QString>
#include <QWidget>

#include "ContentSearchMatch.h"
#include "ContentSearchOptions.h"
#include "FileNode.h"

class QLineEdit;
class QLabel;
class QModelIndex;
class QToolButton;
class QTreeView;
class QItemSelectionModel;
class QTimer;
class ContentSearchResultDelegate;
class ContentSearchResultsModel;

// Content of the left panel's "Find in Files" tab (Architecture.md §14.104): an async, content
// (not name) recursive search over the focused tab's current folder. New sibling to
// SearchPanelWidget's filename-only Search tab -- not a reuse of it, since results are a 2-level
// (file -> line) grouping rather than a flat/tree file listing, and there's a second pair of
// Include/Exclude fields behind a collapsible "Search Details" disclosure. One long-lived
// instance, pushed by MainWindow on focus-retarget/tab signals -- holds no TabViewModel pointer
// of its own, same posture as SearchPanelWidget.
class ContentSearchPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ContentSearchPanelWidget(QWidget* parent = nullptr);

    // QSignalBlocker-guarded resync of the search bar's text (mirrors SearchPanelWidget::setQuery).
    void setQuery(const QString& query);

    // QSignalBlocker-guarded resync of every toggle button and the Include/Exclude fields.
    void setOptions(const ContentSearchOptions& options);

    // Rebuilds the results tree wholesale, re-expands every file-header row, then restores
    // whichever rows the user had manually collapsed before this call (collapse-state survives a
    // live-typing update the same way SearchPanelWidget::collectExpandedPaths/restoreExpandedPaths
    // preserve expansion -- here inverted, since this tree starts fully expanded by default rather
    // than fully collapsed).
    void setResults(const std::vector<FileContentMatch>& matches, const std::filesystem::path& root);

    void setLoading(bool loading);

    // Disabled/empty state for "no focused tab".
    void clearForNoTab();

signals:
    void queryChanged(const QString& query);
    void refreshRequested(const QString& query);
    void clearRequested();

    // Emitted whenever any of the 7 toggle buttons or the Include/Exclude QLineEdits changes.
    void searchOptionsChanged(const ContentSearchOptions& options);

    // File-header row double-click.
    void resultActivated(const std::filesystem::path& path, bool isDirectory);
    // Line row double-click.
    void lineActivated(const std::filesystem::path& path, int oneBasedLine);

    // Bubbled from a single-click on either row kind, resolved against m_lastResults.
    void selectionChanged(const std::vector<FileNode>& entries);

private:
    void onTreeDoubleClicked(const QModelIndex& index);
    void onTreeSelectionChanged();

    // Enforces "at most one of Whole Word/Regex checked, but clicking the active one again
    // unchecks it" for one of the three independent toggle groups below (main query, Include,
    // Exclude) -- same hand-rolled logic SearchPanelWidget::onMatchModeButtonToggled already uses.
    void onMatchModeButtonToggled(QToolButton* button, QToolButton* otherButton, bool checked);

    // Shared tail for every toggle button and both Include/Exclude text edits: recomputes a
    // ContentSearchOptions from current widget state and emits searchOptionsChanged.
    void emitSearchOptionsChanged();

    void updateHeaderCountLabel();
    void setDetailsVisible(bool visible);

    // Current Match Case/Whole Word/Regex state of the main query's toggle buttons, as a
    // TextMatchOptions -- shared by setQuery/setOptions/the search bar's textEdited handler, all of
    // which need to re-apply it to m_delegate's highlight.
    TextMatchOptions currentQueryOptions() const;

    // Top-level (file-header) rows the user has manually collapsed -- walked/restored around every
    // setResults() call so a live-typing update doesn't silently re-expand them.
    std::vector<std::filesystem::path> collectCollapsedPaths() const;
    void restoreCollapsedPaths(const std::vector<std::filesystem::path>& collapsedPaths);

    QToolButton* m_refreshButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QToolButton* m_expandAllButton = nullptr;
    QToolButton* m_collapseAllButton = nullptr;

    QToolButton* m_matchCaseButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_skipGitIgnoredButton = nullptr;

    QLineEdit* m_searchBar = nullptr;
    QTimer* m_searchDebounceTimer = nullptr;

    QToolButton* m_detailsDisclosureButton = nullptr;
    QWidget* m_detailsContent = nullptr;
    QLineEdit* m_includeEdit = nullptr;
    QToolButton* m_includeMatchCaseButton = nullptr;
    QToolButton* m_includeWholeWordButton = nullptr;
    QToolButton* m_includeRegexButton = nullptr;
    QLineEdit* m_excludeEdit = nullptr;
    QToolButton* m_excludeMatchCaseButton = nullptr;
    QToolButton* m_excludeWholeWordButton = nullptr;
    QToolButton* m_excludeRegexButton = nullptr;

    QLabel* m_headerCountLabel = nullptr;

    ContentSearchResultsModel* m_resultsModel = nullptr;
    QTreeView* m_resultsView = nullptr;
    ContentSearchResultDelegate* m_delegate = nullptr;

    // Cache of the last results passed to setResults(), used to resolve a tree-view
    // click/selection back to the full FileNode it belongs to.
    std::vector<FileContentMatch> m_lastResults;
};
