#include "FragmentationResultsModel.h"

#include <algorithm>

#include "FileSizeFormat.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

FragmentationResultsModel::FragmentationResultsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FragmentationResultsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visibleRows.size());
}

int FragmentationResultsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(ColumnCount);
}

QVariant FragmentationResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_visibleRows.size())
    {
        return {};
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    const FileFragmentationInfo& file = m_files[static_cast<size_t>(m_visibleRows[static_cast<size_t>(index.row())])];

    switch (index.column())
    {
        case NameColumn:
            return toQString(file.path.filename());
        case FolderColumn:
            return toQString(file.path.parent_path());
        case SizeColumn:
            return FileSizeFormat::format(file.sizeBytes);
        case FragmentsColumn:
            return static_cast<qulonglong>(file.fragmentCount);
        case StateColumn:
            return file.fragmentCount > 1 ? tr("Fragmented") : tr("OK");
        default:
            return {};
    }
}

QVariant FragmentationResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section)
    {
        case NameColumn:
            return tr("Name");
        case FolderColumn:
            return tr("Folder");
        case SizeColumn:
            return tr("Size");
        case FragmentsColumn:
            return tr("Fragments");
        case StateColumn:
            return tr("State");
        default:
            return {};
    }
}

void FragmentationResultsModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    m_sortColumn = column;
    m_sortAscending = order == Qt::AscendingOrder;
    rebuildVisibleRows();
    endResetModel();
}

std::filesystem::path FragmentationResultsModel::pathAt(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= m_visibleRows.size())
    {
        return {};
    }
    return m_files[static_cast<size_t>(m_visibleRows[static_cast<size_t>(row)])].path;
}

void FragmentationResultsModel::setFiles(const std::vector<FileFragmentationInfo>& files)
{
    beginResetModel();
    m_files = files;
    rebuildVisibleRows();
    endResetModel();
}

void FragmentationResultsModel::setShowOnlyFragmented(bool showOnlyFragmented)
{
    if (showOnlyFragmented == m_showOnlyFragmented)
    {
        return;
    }

    beginResetModel();
    m_showOnlyFragmented = showOnlyFragmented;
    rebuildVisibleRows();
    endResetModel();
}

void FragmentationResultsModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    m_visibleRows.reserve(m_files.size());
    for (int i = 0; i < static_cast<int>(m_files.size()); ++i)
    {
        if (!m_showOnlyFragmented || m_files[static_cast<size_t>(i)].fragmentCount > 1)
        {
            m_visibleRows.push_back(i);
        }
    }

    std::sort(m_visibleRows.begin(), m_visibleRows.end(), [this](int lhs, int rhs) {
        const FileFragmentationInfo& left = m_files[static_cast<size_t>(lhs)];
        const FileFragmentationInfo& right = m_files[static_cast<size_t>(rhs)];

        bool less = false;
        switch (m_sortColumn)
        {
            case FolderColumn:
                less = left.path.parent_path() < right.path.parent_path();
                break;
            case SizeColumn:
                less = left.sizeBytes < right.sizeBytes;
                break;
            case FragmentsColumn:
            case StateColumn: // State is derived from fragmentCount -- same ordering basis.
                less = left.fragmentCount < right.fragmentCount;
                break;
            case NameColumn:
            default:
                less = left.path.filename() < right.path.filename();
                break;
        }
        return m_sortAscending ? less : !less;
    });
}
