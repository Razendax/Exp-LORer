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
#include "FileTagsByPath.h"
#include "FolderSizesByPath.h"
#include "FragmentCountsByPath.h"
#include "GitDirectoryStatus.h"
#include "Tag.h"

// Lets Tag/std::vector<Tag> travel through QVariant (FileListModel::data()'s TagsRole,
// Architecture.md §14.x's Tags column) -- these are plain value types with no Qt dependency of
// their own, so this is the only place that needs to know QVariant can hold them.
Q_DECLARE_METATYPE(Tag)
Q_DECLARE_METATYPE(std::vector<Tag>)

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
        GitStatusColumn,
        TagsColumn,
        DateCreatedColumn,
        PercentColumn,
        FragmentsColumn,
        ColumnCount,
    };

    enum Role
    {
        FilePathRole = Qt::UserRole + 1,
        IsDirectoryRole,
        TagsRole,
        // Raw byte count for SizeColumn, since its DisplayRole is a human-readable "1.5 MB" string
        // (see formatSize in FileListModel.cpp) -- consumers needing the number itself (e.g.
        // FileTileDelegate re-formatting it via QLocale) should read this instead of parsing DisplayRole.
        SizeBytesRole,
        // PercentColumn's raw ratio as a double in [0, 100], for PercentCellDelegate to paint the
        // fill bar without re-parsing DisplayRole's "NN%" text. Invalid when this entry's own size
        // is unknown (an unscanned directory) or the folder's total known size is 0.
        PercentRole,
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

    // Every entry set by the last setEntries() call, regardless of current hidden-file filtering
    // (unlike entryAt(), which only sees m_visibleRows). Used by TabViewModel's tagsChanged handler
    // to re-fetch tags for "whatever this tab's FileListModel currently holds" without needing its
    // own separate cache of the same list (Architecture.md §14.x).
    const std::vector<FileNode>& entries() const noexcept { return m_entries; }

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

    // Sets the per-path git status shown in GitStatusColumn (Architecture.md §14.x), keyed by each
    // entry's absolute path. Called by TabViewModel after GitStatusService's background scan
    // completes; entries with no matching key show no badge (clean, or no repository present).
    void setGitStatuses(const GitStatusByPathMap& statuses);

    // Sets the per-path tags shown in TagsColumn (Architecture.md §14.x), keyed by each entry's
    // absolute path. Called by TabViewModel::requestFileTags, both right after a directory listing
    // and whenever TagChangeNotifier::tagsChanged() fires; a path absent from the map shows no
    // pills (untagged).
    void setFileTags(const TagsByPathMap& tagsByPath);

    // Sets the cached recursive folder sizes shown in SizeColumn for directory rows (Architecture.md
    // §14.39), keyed by each directory's absolute path. Called by TabViewModel::requestFolderSizes
    // right after a directory listing, from a read-only DiskUsageService::requestChildren lookup
    // (never triggers a scan). A directory absent from the map -- never covered by a "Scan Folder"/
    // "Scan Drive" scan -- keeps showing a blank Size cell, same as before this existed.
    void setFolderSizes(const FolderSizesByPathMap& sizesByPath);

    // Sets the cached NTFS fragment counts shown in FragmentsColumn for file rows (Architecture.md
    // §14.53), keyed by each file's absolute path. Called by TabViewModel::requestFragmentCounts
    // right after a directory listing, from a read-only FragmentationService::requestFilesUnder
    // lookup (never triggers a scan). A file absent from the map -- never covered by a "Scan
    // Folder"/"Scan Drive" scan in the Fragmentation tab, or on a non-NTFS volume -- shows a blank
    // Fragments cell, same posture as an uncovered directory's Size cell. Directories always show
    // blank regardless of this map (fragmentation is tracked per file only).
    void setFragmentCounts(const FragmentCountsByPathMap& countsByPath);

    // Merges freshly-resolved icons into m_iconCache, keyed by each entry's absolute path, and
    // repaints NameColumn's DecorationRole for every row. Called incrementally by
    // TabViewModel::requestIcons as IconLoadingService's background worker delivers each chunk, so
    // rows upgrade from the generic placeholder resolveIcon() returns on a cache miss to the real
    // (e.g. an .exe's own PE resource) icon progressively rather than all at once (Architecture.md
    // §14.x).
    void setIcons(const QHash<QString, QIcon>& iconsByPath);

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

    // Shared by SizeColumn/SizeBytesRole and PercentColumn/PercentRole: a file's own size is
    // always known; a directory's is known only if m_folderSizesByPath already has a cached
    // recursive size for it (i.e. covered by a prior "Scan Folder"/"Scan Drive" run,
    // Architecture.md §14.39), otherwise std::nullopt (shown blank, never 0).
    std::optional<std::uintmax_t> knownSizeOf(const FileNode& entry) const;

    // Returns the cached real icon for `entry` if IconLoadingService has already resolved it
    // (m_iconCache hit); otherwise returns a cheap generic placeholder -- m_iconProvider.icon() on
    // the QFileIconProvider::Folder/File enum overload, a static resource lookup, not
    // QFileIconProvider::icon(QFileInfo) (which on Windows runs the comparatively expensive
    // SHGetFileInfo PE-resource extraction for .exe/.dll/.lnk) -- and does NOT insert that
    // placeholder into the cache, so the real icon delivered later by setIcons() still overwrites
    // it (Architecture.md §14.x, "Fix slow folder-open for executable-heavy directories").
    QIcon resolveIcon(const FileNode& entry) const;

    // Recomputes m_totalKnownSize from m_entries via knownSizeOf (unknown entries contribute 0) --
    // the PercentColumn denominator, Architecture.md §14.39's "full children total" basis. Called
    // whenever m_entries or m_folderSizesByPath changes.
    void recomputeTotalSize();

    std::filesystem::path m_directory;
    std::vector<FileNode> m_entries;
    std::vector<int> m_visibleRows;
    // Inverse of m_visibleRows, keyed by each visible entry's absolute path -- lets setIcons()
    // scope its dataChanged emit to the rows an icon batch actually touched instead of repainting
    // the whole column on every ~20-entry chunk delivery. Rebuilt alongside m_visibleRows inside
    // rebuildVisibleRows() so it stays correct after setEntries/setSortCriterion/
    // setShowHiddenFiles. A path not in this map is currently filtered out (hidden-file toggle) or
    // belongs to a stale reply for an already-superseded listing.
    QHash<QString, int> m_rowByPath;
    QFileIconProvider m_iconProvider;
    GitStatusByPathMap m_gitStatusByPath;
    TagsByPathMap m_fileTagsByPath;
    FolderSizesByPathMap m_folderSizesByPath;
    FragmentCountsByPathMap m_fragmentCountsByPath;
    std::uintmax_t m_totalKnownSize = 0;

    // Dimmed (QStyle::generatedIconPixmap) variant of a hidden entry's icon, cached per icon
    // "kind" (extension/directory/symlink, see data()) rather than rebuilt on every data() call --
    // with many hidden entries visible (.git, node_modules, ...) rebuilding per repaint reintroduces
    // the UI-freeze class of issue efd8132 fixed for the base icon.
    mutable QHash<QString, QIcon> m_dimmedIconCache;

    // Per-path real-icon cache, populated incrementally by setIcons() (IconLoadingService's
    // background worker) and consulted by resolveIcon() -- a cache miss means the icon hasn't been
    // resolved yet, not that it's missing. Cleared in setEntries() alongside every other per-
    // directory map, bounding memory to the current directory's entries.
    mutable QHash<QString, QIcon> m_iconCache;

    SortCriterion m_sortCriterion = SortCriterion::Name;
    bool m_sortAscending = true;
    bool m_showHiddenFiles = false;
    const FileDecorationRules& m_fileDecorationRules;
};
