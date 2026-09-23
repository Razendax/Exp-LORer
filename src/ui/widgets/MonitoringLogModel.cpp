#include "MonitoringLogModel.h"

#include <algorithm>

#include <QColor>

#include "UiColors.h"

MonitoringLogModel::MonitoringLogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MonitoringLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant MonitoringLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= m_entries.size())
    {
        return {};
    }

    const FolderChangeEvent& event = m_entries[static_cast<std::size_t>(index.row())];

    if (role == Qt::DisplayRole)
    {
        return QString::fromStdWString(event.relativePath.wstring());
    }

    if (role == Qt::DecorationRole)
    {
        return event.isDirectory ? m_iconProvider.icon(QFileIconProvider::Folder) : m_iconProvider.icon(QFileIconProvider::File);
    }

    if (role == Qt::ForegroundRole)
    {
        switch (event.kind)
        {
            case FolderChangeKind::Added:
                return QColor(QLatin1String(UiColors::kMonitorAddedGreen));
            case FolderChangeKind::Removed:
                return QColor(QLatin1String(UiColors::kMonitorRemovedRed));
            case FolderChangeKind::Modified:
                return QColor(QLatin1String(UiColors::kMonitorModifiedYellow));
        }
    }

    return {};
}

void MonitoringLogModel::setEntries(const std::deque<FolderChangeEvent>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void MonitoringLogModel::appendEntries(const std::vector<FolderChangeEvent>& batch)
{
    if (batch.empty())
    {
        return;
    }

    // Evict oldest entries first so the cap holds even mid-append: for a batch smaller than the
    // cap this is the normal ring-buffer overflow case; for a batch bigger than the cap it
    // degrades gracefully to "keep only the newest kCap".
    const std::size_t prospectiveSize = m_entries.size() + batch.size();
    if (prospectiveSize > kCap)
    {
        const std::size_t toEvict = std::min(m_entries.size(), prospectiveSize - kCap);
        if (toEvict > 0)
        {
            beginRemoveRows(QModelIndex(), 0, static_cast<int>(toEvict) - 1);
            m_entries.erase(m_entries.begin(), m_entries.begin() + static_cast<std::ptrdiff_t>(toEvict));
            endRemoveRows();
        }
    }

    const std::size_t insertCount = std::min(batch.size(), kCap);
    const auto insertBegin = batch.end() - static_cast<std::ptrdiff_t>(insertCount);

    const int firstRow = static_cast<int>(m_entries.size());
    beginInsertRows(QModelIndex(), firstRow, firstRow + static_cast<int>(insertCount) - 1);
    m_entries.insert(m_entries.end(), insertBegin, batch.end());
    endInsertRows();
}
