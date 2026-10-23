#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QAbstractItemModel>
#include <QString>

#include "ContentSearchMatch.h"

// 2-level results model for "Find in Files" (Architecture.md §14.104): top-level rows are one per
// FileContentMatch (a file-group header), each exposing child rows one per LineMatch.
// Deliberately simpler than SearchResultsTreeModel -- no folder-hierarchy grouping, since spec
// calls for a flat "grouped by file" list, not a nested tree. Painting (highlight spans, bold,
// elision) is entirely the new ContentSearchResultDelegate's job -- this model only exposes data.
class ContentSearchResultsModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        // Resolves to the owning file's path at both levels (a line row returns its parent file's
        // path), so callers can read it uniformly regardless of which row kind was activated.
        PathRole = Qt::UserRole + 1,
        IsDirectoryRole, // always false -- content search never matches a directory
        IsHiddenRole,    // FileNode::isHidden(), for the delegate's graying
        LineNumberRole,  // child (line) rows only; 1-based
        FullLineTextRole // child (line) rows only -- for the delegate's highlight re-derivation
    };

    explicit ContentSearchResultsModel(QObject* parent = nullptr);
    ~ContentSearchResultsModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Rebuilds wholesale -- no diffing, same "cheap enough since it's already-in-memory data"
    // posture SearchResultsTreeModel::setResults already has. `root` is used only to compute each
    // header row's path-disambiguation text.
    void setResults(const std::vector<FileContentMatch>& matches, const std::filesystem::path& root);

    // Widget-facing getters (not embedded Qt::DisplayRole text) so ContentSearchPanelWidget builds
    // its own header-count label -- same separation TabQuickSearchViewModel::resultsModel() already
    // keeps (widget decides presentation, model is just data).
    int matchCount() const noexcept { return m_matchCount; }
    int fileCount() const noexcept { return m_fileCount; }

private:
    struct Node;

    Node* nodeFor(const QModelIndex& index) const;
    QModelIndex indexFor(const Node* node) const;

    std::unique_ptr<Node> m_root; // invisible; its children are the per-file header rows
    int m_matchCount = 0;
    int m_fileCount = 0;
};
