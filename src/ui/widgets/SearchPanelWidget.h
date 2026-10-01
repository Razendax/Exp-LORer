#pragma once

#include <filesystem>
#include <vector>

#include <QString>
#include <QWidget>

#include "FileNode.h"

class QLineEdit;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QTreeView;
class QItemSelection;
class FileListModel;
class FileDecorationRules;
class FileBrowserView;
class SearchResultDelegate;
class SearchResultsTreeModel;

// Content of the left panel's "Search" tab (Architecture.md §14.42): an async, name-only recursive
// search over the focused tab's current folder, rendered as a list or (optionally) a folder tree,
// plus the relocated Advanced Search entry point. One long-lived instance (like TagPanelWidget/
// PreviewPanelWidget), pushed by MainWindow on focus-retarget/tab signals -- holds no
// TabViewModel pointer of its own and owns its own small FileListModel/SearchResultsTreeModel
// rather than rebinding to whichever tab is currently focused, since FileBrowserView's model is
// fixed at construction.
class SearchPanelWidget : public QWidget
{
    Q_OBJECT

public:
    SearchPanelWidget(const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource, QWidget* parent = nullptr);

    // QSignalBlocker-guarded resync of the search bar's text (mirrors the old
    // m_searchBar->setText(...) in MainWindow::onFocusedTabChanged) -- also re-applies `query` as
    // the highlight term on both result views.
    void setQuery(const QString& query);

    void setResults(const std::vector<FileNode>& entries, const std::filesystem::path& root);
    void setLoading(bool loading);

    // Disabled/empty state for "no focused tab" (e.g. the last pane's last tab just closed).
    void clearForNoTab();

signals:
    void queryChanged(const QString& query);
    void refreshRequested(const QString& query);
    void clearRequested();
    void advancedSearchRequested();
    void resultActivated(const std::filesystem::path& path, bool isDirectory);

    // Bubbled from whichever result view (list or tree) the user selected in -- empty on
    // deselection or a tree click on a structural ancestor node that isn't itself a matched entry
    // (see m_lastResults).
    void selectionChanged(const std::vector<FileNode>& entries);

private:
    void setViewAsTree(bool tree);
    void onTreeSelectionChanged();

    QToolButton* m_refreshButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QToolButton* m_viewAsTreeButton = nullptr;
    QToolButton* m_collapseAllButton = nullptr;

    QLineEdit* m_searchBar = nullptr;
    QPushButton* m_advancedSearchButton = nullptr;

    QStackedWidget* m_resultsStack = nullptr;
    FileListModel* m_listModel = nullptr;
    FileBrowserView* m_listResultsView = nullptr;
    SearchResultsTreeModel* m_treeModel = nullptr;
    QTreeView* m_treeResultsView = nullptr;
    SearchResultDelegate* m_treeDelegate = nullptr;

    // Cache of the last results passed to setResults(), used to resolve a tree-view selection
    // (path + isDirectory only, per SearchResultsTreeModel::Node) back to a full FileNode.
    std::vector<FileNode> m_lastResults;
};
