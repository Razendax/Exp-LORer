#include "FileListModel.h"

#include <QDateTime>
#include <QFileInfo>

#include "FileNavigationUseCase.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    QString fileTypeLabel(const FileNode& entry)
    {
        if (entry.isDirectory())
        {
            return QObject::tr("File folder");
        }

        const QString extension = toQString(entry.path().extension());
        if (extension.isEmpty())
        {
            return QObject::tr("File");
        }

        return extension.mid(1).toUpper() + QObject::tr(" File");
    }

    QString formatDate(std::chrono::system_clock::time_point timePoint)
    {
        const auto timeT = std::chrono::system_clock::to_time_t(timePoint);
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timeT)).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }

    SortCriterion criterionForColumn(int column)
    {
        switch (column)
        {
            case FileListModel::SizeColumn:
                return SortCriterion::Size;
            case FileListModel::TypeColumn:
                return SortCriterion::FileType;
            case FileListModel::DateModifiedColumn:
                return SortCriterion::ModificationDate;
            case FileListModel::NameColumn:
            default:
                return SortCriterion::Name;
        }
    }
}

FileListModel::FileListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(ColumnCount);
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_entries.size())
    {
        return {};
    }

    const FileNode& entry = m_entries[static_cast<size_t>(index.row())];

    if (role == FilePathRole)
    {
        return toQString(entry.path());
    }
    if (role == IsDirectoryRole)
    {
        return entry.isDirectory();
    }

    if (role == Qt::DecorationRole && index.column() == NameColumn)
    {
        return m_iconProvider.icon(QFileInfo(toQString(entry.path())));
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
        case NameColumn:
            return toQString(entry.name());
        case SizeColumn:
            return entry.isDirectory() ? QVariant() : QVariant(static_cast<qulonglong>(entry.size()));
        case TypeColumn:
            return fileTypeLabel(entry);
        case DateModifiedColumn:
            return formatDate(entry.modificationDate());
        default:
            return {};
    }
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section)
    {
        case NameColumn:
            return tr("Name");
        case SizeColumn:
            return tr("Size");
        case TypeColumn:
            return tr("Type");
        case DateModifiedColumn:
            return tr("Date modified");
        default:
            return {};
    }
}

void FileListModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    m_entries = FileNavigationUseCase::sortBy(std::move(m_entries), criterionForColumn(column), order == Qt::AscendingOrder);
    endResetModel();
}

void FileListModel::setEntries(const std::filesystem::path& directory, const std::vector<FileNode>& entries)
{
    Q_UNUSED(directory);

    beginResetModel();
    m_entries = entries;
    endResetModel();
}
