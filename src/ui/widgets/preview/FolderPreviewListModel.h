#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QFileIconProvider>

#include "FileNode.h"

// Qt-facing list model backing the Preview panel's folder listing (PreviewPanelWidget::showFolder).
// Used behind a QListView instead of an eagerly-populated QListWidget so that
// QFileIconProvider::icon() -- expensive per call for executables, whose icons are extracted from
// each file's own PE resources rather than shared via the extension-icon cache most file types use
// -- is only invoked for rows the view actually paints (its viewport), not for every entry in the
// directory up front. Mirrors FileListModel's lazy Qt::DecorationRole pattern.
class FolderPreviewListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit FolderPreviewListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void setEntries(const std::vector<FileNode>& entries);

private:
    std::vector<FileNode> m_entries;
    QFileIconProvider m_iconProvider;
};
