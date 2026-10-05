#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <QAbstractItemModel>
#include <QString>

#include "DuplicateFinderWorker.h"

// Grouped, checkable duplicate-file results for DuplicateFinderDialog (Architecture.md §14.51).
// Not a reused FileListModel -- grouped/checkable rows with a per-row checkbox don't fit
// FileListModel's per-folder-listing shape. Exactly two levels: top-level rows are groups ("12
// files, 4.0 MB each -- 44.0 MB wasted"); their children are the group's member files, each with a
// checkbox, full path, containing folder, and modified date.
class DuplicateGroupsModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn = 0,
        FolderColumn,
        ModifiedColumn,
        ColumnCount,
    };

    enum Role
    {
        PathRole = Qt::UserRole + 1,
    };

    explicit DuplicateGroupsModel(QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setGroups(std::vector<DuplicateGroupInfo> groups);
    void clear();

    bool isFileRow(const QModelIndex& index) const;
    std::filesystem::path pathForIndex(const QModelIndex& index) const;

    std::vector<std::filesystem::path> checkedPaths() const;
    std::uintmax_t checkedBytes() const;

    // "Keep one, delete the rest" helpers: check every member of every group except the
    // newest/oldest by modification date (ties keep the first-encountered extremum unchecked).
    // Individual checkboxes remain freely togglable afterward.
    void checkAllButNewestInEachGroup();
    void checkAllButOldestInEachGroup();

    // Removes `paths` from the model after a successful delete. A group that drops to a single
    // remaining member collapses out of the results entirely -- nothing left to call a duplicate.
    void removePaths(const std::vector<std::filesystem::path>& paths);

private:
    struct FileRow
    {
        std::filesystem::path path;
        std::chrono::system_clock::time_point modifiedAt;
        bool checked = false;
    };

    struct GroupRow
    {
        std::uintmax_t fileSize = 0;
        std::vector<FileRow> files;
    };

    QString groupLabel(const GroupRow& group) const;
    void checkAllButExtremumInEachGroup(bool keepNewest);

    std::vector<GroupRow> m_groups;
};
