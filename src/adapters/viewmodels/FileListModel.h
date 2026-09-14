#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QAbstractTableModel>
#include <QFileIconProvider>

#include "FileNode.h"

// Qt-facing model wrapping directory-listing results (FileNode lists) supplied by
// TabViewModel, which owns one instance per tab. Feeds both QListView (icon/list/tiles view
// modes, which only read column 0) and QTreeView (details view mode, all columns) —
// Architecture.md §2.3.1. This supersedes the earlier two-ViewModel
// (FileTreeViewModel/FileGridViewModel) sketch.
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn = 0,
        SizeColumn,
        TypeColumn,
        DateModifiedColumn,
        ColumnCount,
    };

    enum Role
    {
        FilePathRole = Qt::UserRole + 1,
        IsDirectoryRole,
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Header-click sorting hook, invoked automatically by QTreeView when sorting is enabled.
    // Maps the clicked column to a SortCriterion and delegates to FileNavigationUseCase::sortBy
    // (static, no I/O).
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Recovers the full FileNode for a row (already held in m_entries), e.g. to resolve a
    // QItemSelectionModel's current index into a tagging target.
    std::optional<FileNode> entryAt(int row) const;

public slots:
    void setEntries(const std::filesystem::path& directory, const std::vector<FileNode>& entries);

private:
    std::vector<FileNode> m_entries;
    QFileIconProvider m_iconProvider;
};
