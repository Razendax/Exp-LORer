#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QMetaObject>
#include <QString>

#include "FileListModel.h"
#include "FileNode.h"

class FileNavigationUseCase;
class TabViewModel;

// Lazily-populated, folders-only tree backing the left panel's "Tree" tab (Architecture.md
// §14.41): an invisible root -> one "This PC" node (VirtualPaths::ThisPC) -> its drives and
// quick-access folders -> real subfolders, one level listed per expand via FileNavigationUseCase.
// Owned by WorkspaceController and retargeted to the focused pane's active tab the same way as
// TagListViewModel/FilePreviewViewModel -- the UI never constructs one (§14.6).
//
// Single column; column 0 deliberately lines up with FileListModel::NameColumn and answers
// FileListModel's FilePathRole/IsDirectoryRole, so FileNameEditDelegate works on it unchanged.
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        PathRole = FileListModel::FilePathRole,
        IsDirectoryRole = FileListModel::IsDirectoryRole,
    };

    explicit FolderTreeModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);
    ~FolderTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // Editable (in-place rename) only for real folders below This PC's own entries -- This PC,
    // drive roots and quick-access folders are never renameable from the tree.
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Optimistic for a not-yet-listed node (an expand arrow until proven otherwise, like
    // Explorer's navigation pane); exact once listed.
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    // Walks from "This PC" down to `path`, listing any not-yet-loaded level synchronously on the
    // way, and returns the deepest node reached (a vanished folder stops the walk early). Hidden
    // folders along the way are shown even with hidden files off, so the tree can always follow
    // the active tab. A path inside an archive stops at the archive's containing folder -- archives
    // are files, so they never appear in the tree. Invalid index for an empty path or one no
    // This PC entry contains (e.g. a UNC share).
    QModelIndex indexForPath(const std::filesystem::path& path);

    std::filesystem::path pathForIndex(const QModelIndex& index) const;

    // True for "This PC" and its direct children (drive roots, quick-access folders): nodes the
    // tree never offers rename/delete for.
    bool isTopLevelLocation(const QModelIndex& index) const;

    // Path the active tab is currently showing (empty with no active tab) -- what the view reveals.
    std::filesystem::path activePath() const { return m_activePath; }

    // Retargets the tree at `tab` (nullptr allowed): emits activePathChanged now and on every
    // later navigation of that tab, and feeds that tab's own listings into applyListing() so
    // folders it sees appear/disappear in the tree with no extra I/O.
    void setActiveTab(TabViewModel* tab);

public slots:
    // Re-lists `directory` in every loaded node showing it (it can appear twice: under its drive
    // and under a quick-access folder), inserting/removing child rows by diff so surviving
    // children keep their own loaded/expanded subtrees. No-op for a node not listed yet.
    void refreshDirectory(const std::filesystem::path& directory);

    // Same diff as refreshDirectory() but from an already-fetched listing (files in `entries` are
    // ignored) -- no disk I/O.
    void applyListing(const std::filesystem::path& directory, const std::vector<FileNode>& entries);

    // Rewrites a renamed folder's node and every loaded descendant's path in place, then re-sorts
    // it among its siblings, so an expanded subtree survives the rename.
    void renameNode(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Live-toggles hidden-folder filtering by re-listing every loaded node. No-op if unchanged.
    void setShowHiddenFiles(bool show);

signals:
    void activePathChanged(const std::filesystem::path& path);

private:
    struct Node;

    Node* nodeFor(const QModelIndex& index) const;
    QModelIndex indexFor(const Node* node) const;

    void loadChildren(Node* node);
    void applyEntries(Node* node, const std::vector<FileNode>& entries);
    std::vector<Node*> loadedNodesFor(const std::filesystem::path& path) const;
    std::vector<Node*> nodesFor(const std::filesystem::path& path) const;
    void collectLoaded(Node* node, std::vector<Node*>& out) const;

    FileNavigationUseCase& m_fileNavigationUseCase;
    std::unique_ptr<Node> m_root; // invisible; its only child is "This PC"
    bool m_showHiddenFiles = false;
    // Last target of indexForPath(). Hidden folders on the way to it are listed even while
    // m_showHiddenFiles is off (Architecture.md §14.41).
    std::filesystem::path m_revealedPath;

    std::filesystem::path m_activePath;
    QMetaObject::Connection m_pathConnection;
    QMetaObject::Connection m_listingConnection;

    QFileIconProvider m_iconProvider;
};
