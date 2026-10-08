#pragma once

#include <QStyledItemDelegate>

// Inline in-place rename editor for the Name column (Architecture.md §14.13.4/§14.13.6),
// replacing the old QInputDialog-based rename. Used directly as QListView/QTreeView's item
// delegate in List/Details view mode, and as the base class of FileIconDelegate/FileTileDelegate
// so Icons/Tiles view modes inherit the same editing behavior without duplicating it.
//
// Deliberately does not call QAbstractItemModel::setData(): committing a rename has to go through
// FileOperationsController::renamePath and react to a Result failure (operationFailed), which
// setData() has no clean way to surface. setModelData() instead emits renameCommitted() and lets
// the caller (FileBrowserView) resolve the old path and invoke the rename.
class FileNameEditDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FileNameEditDelegate(QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

signals:
    void renameCommitted(const QModelIndex& index, const QString& newName);
};
