#pragma once

#include <filesystem>

#include <QWidget>

#include "ViewMode.h"

class QModelIndex;
class QStackedWidget;
class QListView;
class QTreeView;
class QAbstractItemDelegate;
class FileListModel;
class FileTileDelegate;

// The tab's content widget: a QStackedWidget switching between a QListView (icon/list/tiles view
// modes) and a QTreeView (details view mode), both bound to the same FileListModel
// (Architecture.md §2.3.1).
class FileBrowserView : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserView(FileListModel* model, QWidget* parent = nullptr);

    void setViewMode(ViewMode mode);

signals:
    // Emitted on double-click/Enter on a row; isDirectory decides whether MainWindow forwards
    // this to NavigationViewModel::navigateTo (file activation is out of scope, no viewer yet).
    void itemActivated(const std::filesystem::path& path, bool isDirectory);

private:
    void emitActivated(const QModelIndex& index);

    FileListModel* m_model = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListView* m_listView = nullptr;
    QTreeView* m_treeView = nullptr;
    QAbstractItemDelegate* m_defaultDelegate = nullptr;
    FileTileDelegate* m_tileDelegate = nullptr;
};
