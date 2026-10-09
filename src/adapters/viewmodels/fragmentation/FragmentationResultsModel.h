#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QAbstractTableModel>

#include "FileFragmentationInfo.h"

// Flat results table backing the Fragmentation bottom-panel tab's QTreeView (Architecture.md
// §14.52) -- simpler than DuplicateGroupsModel, since there's no group/member hierarchy here, just
// one row per file. Default-sorted by FragmentsColumn descending, so the most-fragmented files
// surface first without the user sorting manually.
class FragmentationResultsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn = 0,
        FolderColumn,
        SizeColumn,
        FragmentsColumn,
        StateColumn,
        ColumnCount,
    };

    explicit FragmentationResultsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Header-click sorting hook, invoked automatically by QTreeView when sorting is enabled.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Recovers the full path for a visible row, e.g. for the "Open Containing Folder" context-menu
    // action. An empty path for an out-of-range row.
    std::filesystem::path pathAt(int row) const;

public slots:
    // Replaces the full file list (a fresh scan, or a cache-only redisplay alike) -- re-applies the
    // current sort/filter.
    void setFiles(const std::vector<FileFragmentationInfo>& files);

    // Live-toggles the "fragmented only" filter in place (no re-scan, no re-query) -- no-op if
    // unchanged.
    void setShowOnlyFragmented(bool showOnlyFragmented);

private:
    void rebuildVisibleRows();

    std::vector<FileFragmentationInfo> m_files;
    std::vector<int> m_visibleRows;
    bool m_showOnlyFragmented = true;

    // Default: Fragments descending (Architecture.md §14.52).
    int m_sortColumn = FragmentsColumn;
    bool m_sortAscending = false;
};
