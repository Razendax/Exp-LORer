#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QAbstractTableModel>
#include <QFileIconProvider>
#include <QHash>
#include <QIcon>
#include <QString>

#include "FileDecorationRules.h"
#include "FileNavigationUseCase.h"
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

    // `fileDecorationsChangeSource` is a bare QObject& (not FileDecorationsViewModel&, the concrete
    // Qt-facing owner in src/adapters/config) deliberately: src/adapters/config already depends on
    // src/adapters/viewmodels (AppConfig needs FileListModel::ColumnCount), so a
    // FileDecorationsViewModel& constructor parameter here would create a build-graph cycle.
    // FileListModel connects to its rulesChanged() signal via the string-based SIGNAL/SLOT
    // overload of connect() instead, which needs no compile-time knowledge of the concrete type
    // (Architecture.md §14.29).
    FileListModel(const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Marks the Name column editable for real filesystem entries (excludes synthetic rows like
    // "This PC"'s drives, which have a displayName() and no renameable path) so views can open an
    // in-place editor via QAbstractItemView::edit(). Deliberately no setData() override: committing
    // a rename has to go through FileOperationsController::renamePath and react to failure, which a
    // model's setData() has no clean way to do — FileNameEditDelegate intercepts the commit instead
    // and never calls setData().
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Header-click sorting hook, invoked automatically by QTreeView when sorting is enabled.
    // Maps the clicked column to a SortCriterion and delegates to setSortCriterion.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Recovers the full FileNode for a row (already held in m_entries), e.g. to resolve a
    // QItemSelectionModel's current index into a tagging target.
    std::optional<FileNode> entryAt(int row) const;

    // Directory these entries were listed from (last argument passed to setEntries), so views can
    // detect whether a navigation attempt actually changed directory and locate a row by path
    // without depending on any single entry being present.
    const std::filesystem::path& currentDirectory() const noexcept { return m_directory; }

    SortCriterion sortCriterion() const noexcept { return m_sortCriterion; }
    bool sortAscending() const noexcept { return m_sortAscending; }

    // Inverse of the column<->criterion mapping used by sort()/criterionForColumn, so
    // FileBrowserView can drive the QTreeView header's sort indicator from a SortCriterion.
    static int columnForCriterion(SortCriterion criterion);

    // Settings key backing the global "show hidden files/folders" preference, mirroring
    // WorkspacePaneWidget::kShowShellExtensionsSettingsKey's QSettings-based-toggle pattern
    // (Architecture.md §14.14).
    static constexpr const char* kShowHiddenFilesSettingsKey = "View/ShowHiddenFiles";
    static bool showHiddenFilesEnabled();

public slots:
    void setEntries(const std::filesystem::path& directory, const std::vector<FileNode>& entries);

    // Re-sorts m_entries in place via FileNavigationUseCase::sortBy and emits sortOrderChanged.
    // No-op if criterion/ascending are unchanged.
    void setSortCriterion(SortCriterion criterion, bool ascending);

    // Live-toggles hidden-entry filtering without re-navigating/rescanning. No-op if unchanged.
    void setShowHiddenFiles(bool show);

signals:
    void sortOrderChanged(SortCriterion criterion, bool ascending);

private slots:
    // Connected to fileDecorationsChangeSource's rulesChanged() signal (Architecture.md §14.29).
    // Emits dataChanged() for every row across ForegroundRole/FontRole so already-open folders/
    // search results restyle live when a rule is added/edited/removed/reordered -- a full model
    // reset is unnecessary and would lose selection/scroll position.
    void handleFileDecorationsChanged();

private:
    // Rebuilds m_visibleRows from m_entries/m_showHiddenFiles. Caller is responsible for the
    // surrounding beginResetModel()/endResetModel() pair.
    void rebuildVisibleRows();

    // std::nullopt for synthetic rows (entry.displayName() set, e.g. "This PC"'s drives) and for
    // any entry no rule matches.
    std::optional<FileDecorationRule> resolveDecoration(const FileNode& entry) const;

    std::filesystem::path m_directory;
    std::vector<FileNode> m_entries;
    std::vector<int> m_visibleRows;
    QFileIconProvider m_iconProvider;

    // Dimmed (QStyle::generatedIconPixmap) variant of a hidden entry's icon, cached per icon
    // "kind" (extension/directory/symlink, see data()) rather than rebuilt on every data() call --
    // with many hidden entries visible (.git, node_modules, ...) rebuilding per repaint reintroduces
    // the UI-freeze class of issue efd8132 fixed for the base icon.
    mutable QHash<QString, QIcon> m_dimmedIconCache;

    SortCriterion m_sortCriterion = SortCriterion::Name;
    bool m_sortAscending = true;
    bool m_showHiddenFiles = false;
    const FileDecorationRules& m_fileDecorationRules;
};
