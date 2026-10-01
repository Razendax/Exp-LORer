#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QString>

#include "FileListModel.h"
#include "FileNode.h"

// Hierarchical grouping of the Search tab's own flat result snapshot (Architecture.md §14.42),
// relative to the search root -- the "view as tree" mode next to the plain list view. Unlike
// FolderTreeModel, this has no lazy loading/fetchMore: the whole result set is already in memory
// (TabViewModel's search snapshot/partial snapshot), so setResults() rebuilds the tree wholesale
// on every call rather than diffing.
//
// Single column; column 0 lines up with FileListModel::NameColumn so SearchResultDelegate's
// paint-the-Name-column logic works on this model unchanged.
class SearchResultsTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        PathRole = FileListModel::FilePathRole,
        IsDirectoryRole = FileListModel::IsDirectoryRole,
    };

    explicit SearchResultsTreeModel(QObject* parent = nullptr);
    ~SearchResultsTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // The search root `matches` are relative to -- call before/with every setResults().
    void setRoot(const std::filesystem::path& root);

    // Rebuilds the branch/leaf tree from scratch: for each matched FileNode, the path segments
    // between `root` and the node itself become (shared, one-per-unique-folder) ancestor branch
    // nodes, ending in a leaf for the match itself. A folder that itself matched the query is a
    // leaf like any other hit -- its own matching descendants, if any, simply appear as deeper
    // leaves under it, since they go through the same path-splitting logic.
    void setResults(const std::vector<FileNode>& matches);

    std::filesystem::path pathForIndex(const QModelIndex& index) const;
    bool isDirectoryForIndex(const QModelIndex& index) const;

private:
    struct Node;

    Node* nodeFor(const QModelIndex& index) const;
    QModelIndex indexFor(const Node* node) const;
    void sortChildren(Node* node);

    std::unique_ptr<Node> m_root; // invisible; its children are the top-level hits/ancestor folders
    std::filesystem::path m_searchRoot;
    QFileIconProvider m_iconProvider;
};
