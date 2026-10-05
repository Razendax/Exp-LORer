#include "DuplicateGroupsModel.h"

#include <algorithm>

#include <QDateTime>

#include "FileSizeFormat.h"

namespace
{
    // Top-level (group) rows are tagged with this internalId sentinel; file rows carry their
    // parent group's index instead -- the standard two-level QAbstractItemModel idiom.
    constexpr quintptr kGroupRowSentinel = static_cast<quintptr>(-1);

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    QString formatDate(std::chrono::system_clock::time_point timePoint)
    {
        const auto timeT = std::chrono::system_clock::to_time_t(timePoint);
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timeT)).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
}

DuplicateGroupsModel::DuplicateGroupsModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex DuplicateGroupsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
    {
        return QModelIndex();
    }

    if (!parent.isValid())
    {
        if (static_cast<std::size_t>(row) >= m_groups.size())
        {
            return QModelIndex();
        }
        return createIndex(row, column, kGroupRowSentinel);
    }

    if (parent.internalId() != kGroupRowSentinel || parent.column() != 0)
    {
        return QModelIndex(); // file rows have no children
    }

    const auto groupIndex = static_cast<std::size_t>(parent.row());
    if (groupIndex >= m_groups.size() || static_cast<std::size_t>(row) >= m_groups[groupIndex].files.size())
    {
        return QModelIndex();
    }
    return createIndex(row, column, static_cast<quintptr>(groupIndex));
}

QModelIndex DuplicateGroupsModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() == kGroupRowSentinel)
    {
        return QModelIndex();
    }
    return createIndex(static_cast<int>(child.internalId()), 0, kGroupRowSentinel);
}

int DuplicateGroupsModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
    {
        return static_cast<int>(m_groups.size());
    }
    if (parent.internalId() != kGroupRowSentinel || parent.column() != 0)
    {
        return 0;
    }
    const auto groupIndex = static_cast<std::size_t>(parent.row());
    if (groupIndex >= m_groups.size())
    {
        return 0;
    }
    return static_cast<int>(m_groups[groupIndex].files.size());
}

int DuplicateGroupsModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant DuplicateGroupsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    if (index.internalId() == kGroupRowSentinel)
    {
        const auto& group = m_groups[static_cast<std::size_t>(index.row())];
        if (index.column() == NameColumn && role == Qt::DisplayRole)
        {
            return groupLabel(group);
        }
        return QVariant();
    }

    const auto groupIndex = static_cast<std::size_t>(index.internalId());
    const auto& file = m_groups[groupIndex].files[static_cast<std::size_t>(index.row())];

    switch (role)
    {
        case Qt::CheckStateRole:
            return index.column() == NameColumn ? QVariant(file.checked ? Qt::Checked : Qt::Unchecked) : QVariant();
        case Qt::DisplayRole:
            switch (index.column())
            {
                case NameColumn:
                    return toQString(file.path.filename());
                case FolderColumn:
                    return toQString(file.path.parent_path());
                case ModifiedColumn:
                    return formatDate(file.modifiedAt);
                default:
                    return QVariant();
            }
        case Qt::ToolTipRole:
            return toQString(file.path);
        case PathRole:
            return toQString(file.path);
        default:
            return QVariant();
    }
}

bool DuplicateGroupsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.internalId() == kGroupRowSentinel || role != Qt::CheckStateRole || index.column() != NameColumn)
    {
        return false;
    }

    const auto groupIndex = static_cast<std::size_t>(index.internalId());
    auto& file = m_groups[groupIndex].files[static_cast<std::size_t>(index.row())];
    file.checked = (value.toInt() == Qt::Checked);

    emit dataChanged(index, index, { Qt::CheckStateRole });
    return true;
}

Qt::ItemFlags DuplicateGroupsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.internalId() != kGroupRowSentinel && index.column() == NameColumn)
    {
        itemFlags |= Qt::ItemIsUserCheckable;
    }
    return itemFlags;
}

QVariant DuplicateGroupsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QVariant();
    }

    switch (section)
    {
        case NameColumn:
            return tr("Name");
        case FolderColumn:
            return tr("Folder");
        case ModifiedColumn:
            return tr("Modified");
        default:
            return QVariant();
    }
}

void DuplicateGroupsModel::setGroups(std::vector<DuplicateGroupInfo> groups)
{
    beginResetModel();

    m_groups.clear();
    m_groups.reserve(groups.size());
    for (auto& group : groups)
    {
        GroupRow row;
        row.fileSize = group.group.fileSize;
        row.files.reserve(group.files.size());
        for (auto& file : group.files)
        {
            row.files.push_back(FileRow{ std::move(file.path), file.modifiedAt, false });
        }
        m_groups.push_back(std::move(row));
    }

    endResetModel();
}

void DuplicateGroupsModel::clear()
{
    beginResetModel();
    m_groups.clear();
    endResetModel();
}

bool DuplicateGroupsModel::isFileRow(const QModelIndex& index) const
{
    return index.isValid() && index.internalId() != kGroupRowSentinel;
}

std::filesystem::path DuplicateGroupsModel::pathForIndex(const QModelIndex& index) const
{
    if (!isFileRow(index))
    {
        return std::filesystem::path();
    }
    const auto groupIndex = static_cast<std::size_t>(index.internalId());
    return m_groups[groupIndex].files[static_cast<std::size_t>(index.row())].path;
}

std::vector<std::filesystem::path> DuplicateGroupsModel::checkedPaths() const
{
    std::vector<std::filesystem::path> result;
    for (const auto& group : m_groups)
    {
        for (const auto& file : group.files)
        {
            if (file.checked)
            {
                result.push_back(file.path);
            }
        }
    }
    return result;
}

std::uintmax_t DuplicateGroupsModel::checkedBytes() const
{
    std::uintmax_t total = 0;
    for (const auto& group : m_groups)
    {
        for (const auto& file : group.files)
        {
            if (file.checked)
            {
                total += group.fileSize;
            }
        }
    }
    return total;
}

QString DuplicateGroupsModel::groupLabel(const GroupRow& group) const
{
    const auto count = group.files.size();
    const std::uintmax_t wasted = count > 0 ? (count - 1) * group.fileSize : 0;
    return tr("%1 files, %2 each -- %3 wasted")
        .arg(count)
        .arg(FileSizeFormat::format(group.fileSize))
        .arg(FileSizeFormat::format(wasted));
}

void DuplicateGroupsModel::checkAllButExtremumInEachGroup(bool keepNewest)
{
    for (std::size_t groupIndex = 0; groupIndex < m_groups.size(); ++groupIndex)
    {
        auto& files = m_groups[groupIndex].files;
        if (files.empty())
        {
            continue;
        }

        std::size_t extremumIndex = 0;
        for (std::size_t i = 1; i < files.size(); ++i)
        {
            const bool isMoreExtreme =
                keepNewest ? (files[i].modifiedAt > files[extremumIndex].modifiedAt) : (files[i].modifiedAt < files[extremumIndex].modifiedAt);
            if (isMoreExtreme)
            {
                extremumIndex = i;
            }
        }

        for (std::size_t i = 0; i < files.size(); ++i)
        {
            files[i].checked = (i != extremumIndex);
        }

        const QModelIndex parentIndex = index(static_cast<int>(groupIndex), 0, QModelIndex());
        emit dataChanged(index(0, NameColumn, parentIndex), index(static_cast<int>(files.size()) - 1, NameColumn, parentIndex),
                          { Qt::CheckStateRole });
    }
}

void DuplicateGroupsModel::checkAllButNewestInEachGroup()
{
    checkAllButExtremumInEachGroup(true);
}

void DuplicateGroupsModel::checkAllButOldestInEachGroup()
{
    checkAllButExtremumInEachGroup(false);
}

void DuplicateGroupsModel::removePaths(const std::vector<std::filesystem::path>& paths)
{
    if (paths.empty())
    {
        return;
    }

    beginResetModel();

    for (auto groupIt = m_groups.begin(); groupIt != m_groups.end();)
    {
        auto& files = groupIt->files;
        files.erase(std::remove_if(files.begin(), files.end(),
                                    [&paths](const FileRow& file) {
                                        return std::find(paths.begin(), paths.end(), file.path) != paths.end();
                                    }),
                    files.end());

        // A group that drops to a single remaining member is no longer a duplicate of anything.
        if (files.size() <= 1)
        {
            groupIt = m_groups.erase(groupIt);
        }
        else
        {
            ++groupIt;
        }
    }

    endResetModel();
}
