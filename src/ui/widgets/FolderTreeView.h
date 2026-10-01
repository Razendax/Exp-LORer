#pragma once

#include <filesystem>

#include <QPoint>
#include <QTreeView>

class FolderTreeModel;
class QKeyEvent;
class QShowEvent;

// The left panel's "Tree" tab (Architecture.md §14.41): a folders-only QTreeView over the
// WorkspaceController-owned FolderTreeModel. Follows the focused tab -- every
// FolderTreeModel::activePathChanged expands the path's ancestors and selects it -- and turns user
// input into signals for MainWindow to act on (navigation, rename, delete, context menu), same
// posture as FileBrowserView. While hidden (panel collapsed) it skips revealing entirely and
// catches up in showEvent, so a collapsed panel costs no directory listing.
class FolderTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit FolderTreeView(FolderTreeModel* model, QWidget* parent = nullptr);

    // Opens the inline rename editor on `path`'s node, if the tree shows it and it's renameable
    // (not This PC/a drive/a quick-access folder).
    void beginRename(const std::filesystem::path& path);

    // Expands every ancestor of `path`'s node and selects it. Programmatic, so it never emits
    // navigateRequested. Deferred to the next showEvent while hidden.
    void revealPath(const std::filesystem::path& path);

signals:
    // Single click on a folder's row (not its expand arrow), or Enter.
    void navigateRequested(const std::filesystem::path& path);

    // Inline rename committed (FileNameEditDelegate), destination = same folder, new name.
    void renameRequested(const std::filesystem::path& source, const std::filesystem::path& destination);

    // Delete (permanent = false) / Shift+Delete (permanent = true) on the current folder.
    void deleteRequested(const std::filesystem::path& path, bool permanent);

    // Right-click on a folder row (never on This PC or empty space). `isTopLevelLocation` marks a
    // drive root or quick-access folder, for which rename/delete are not offered.
    void itemContextMenuRequested(const std::filesystem::path& path, bool isTopLevelLocation, const QPoint& globalPos);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void onRenameCommitted(const QModelIndex& index, const QString& newName);
    void onCustomContextMenuRequested(const QPoint& pos);

    FolderTreeModel* m_model = nullptr;
    bool m_revealPending = true;
};
