#include "FolderPreviewListModel.h"

#include <QFileInfo>

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

FolderPreviewListModel::FolderPreviewListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FolderPreviewListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant FolderPreviewListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_entries.size())
    {
        return {};
    }

    const FileNode& entry = m_entries[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole)
    {
        return entry.displayName() ? QString::fromStdString(*entry.displayName()) : toQString(entry.name());
    }

    if (role == Qt::DecorationRole)
    {
        return m_iconProvider.icon(QFileInfo(toQString(entry.path())));
    }

    return {};
}

void FolderPreviewListModel::setEntries(const std::vector<FileNode>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}
