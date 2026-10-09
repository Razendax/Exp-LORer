#include "FileListModel.h"

#include <algorithm>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStyle>
#include <QStyleOption>

#include "FileSizeFormat.h"
#include "PathUtf8.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    QString fileTypeLabel(const FileNode& entry)
    {
        if (entry.fileType() == FileType::Symlink)
        {
            return QObject::tr("Symbolic link");
        }

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
            case FileListModel::PercentColumn:
                return SortCriterion::Percent;
            case FileListModel::NameColumn:
            default:
                return SortCriterion::Name;
        }
    }

    int columnForCriterion(SortCriterion criterion)
    {
        switch (criterion)
        {
            case SortCriterion::Size:
                return FileListModel::SizeColumn;
            case SortCriterion::FileType:
                return FileListModel::TypeColumn;
            case SortCriterion::ModificationDate:
                return FileListModel::DateModifiedColumn;
            case SortCriterion::Percent:
                return FileListModel::PercentColumn;
            case SortCriterion::Name:
            default:
                return FileListModel::NameColumn;
        }
    }

    // Directories synthesized by archive-browsing (Architecture.md §14.28) have no real path on
    // disk, so QFileIconProvider::icon(QFileInfo) can't stat them and falls back to a generic/file
    // icon instead of a folder icon -- callers needing the real (non-placeholder) icon force the
    // QFileIconProvider::Folder enum icon for these instead.
    bool archiveSynthesizedDir(const FileNode& entry)
    {
        return entry.isDirectory() && !QFileInfo(toQString(entry.path())).exists();
    }

    // Hidden files/folders keep the classic dimmed-icon look (Architecture.md §14.21) independent
    // of §14.29's now-configurable text/font styling -- rebuilds `icon` with every available size
    // run through QStyle::generatedIconPixmap(QIcon::Disabled, ...), the same graying Qt applies to
    // a disabled toolbar action, so it already tracks the light/dark palette.
    QIcon dimmedIcon(const QIcon& icon)
    {
        if (icon.isNull())
        {
            return icon;
        }

        QList<QSize> sizes = icon.availableSizes();
        if (sizes.isEmpty())
        {
            sizes = { QSize(16, 16), QSize(32, 32), QSize(48, 48) };
        }

        QStyleOption option;
        option.palette = QApplication::palette();

        QIcon result;
        for (const QSize& size : sizes)
        {
            const QPixmap normalPixmap = icon.pixmap(size);
            result.addPixmap(QApplication::style()->generatedIconPixmap(QIcon::Disabled, normalPixmap, &option));
        }
        return result;
    }

    // Colored circle + glyph per GitFileStatus (Architecture.md §14.x's status/badge table),
    // rendered once via QPainter and cached per value -- 8 entries, trivial, same caching idea as
    // m_dimmedIconCache above.
    QIcon gitStatusBadgeIcon(GitFileStatus status)
    {
        static QHash<int, QIcon> cache;
        const auto cached = cache.constFind(static_cast<int>(status));
        if (cached != cache.constEnd())
        {
            return cached.value();
        }

        QColor color;
        QString glyph;
        switch (status)
        {
            case GitFileStatus::Untracked:
                color = QColor(155, 89, 182);
                glyph = QStringLiteral("?");
                break;
            case GitFileStatus::ModifiedStaged:
                color = QColor(52, 152, 219);
                glyph = QStringLiteral("M");
                break;
            case GitFileStatus::ModifiedUnstaged:
                color = QColor(241, 196, 15);
                glyph = QStringLiteral("M");
                break;
            case GitFileStatus::AddedStaged:
                color = QColor(46, 204, 113);
                glyph = QStringLiteral("+");
                break;
            case GitFileStatus::DeletedStaged:
                color = QColor(231, 76, 60);
                glyph = QStringLiteral("-");
                break;
            case GitFileStatus::DeletedUnstaged:
                color = QColor(230, 126, 34);
                glyph = QStringLiteral("-");
                break;
            case GitFileStatus::Conflicted:
                color = QColor(230, 126, 34);
                glyph = QStringLiteral("C");
                break;
            case GitFileStatus::Ignored:
                color = QColor(149, 165, 166);
                glyph = QStringLiteral("I");
                break;
        }

        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(0, 0, 16, 16);

        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(11);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, 16, 16), Qt::AlignCenter, glyph);
        painter.end();

        const QIcon icon(pixmap);
        cache.insert(static_cast<int>(status), icon);
        return icon;
    }

    QString gitStatusTooltip(GitFileStatus status)
    {
        switch (status)
        {
            case GitFileStatus::Untracked:
                return QObject::tr("Untracked");
            case GitFileStatus::ModifiedStaged:
                return QObject::tr("Modified (staged)");
            case GitFileStatus::ModifiedUnstaged:
                return QObject::tr("Modified (unstaged)");
            case GitFileStatus::AddedStaged:
                return QObject::tr("Added (staged)");
            case GitFileStatus::DeletedStaged:
                return QObject::tr("Deleted (staged)");
            case GitFileStatus::DeletedUnstaged:
                return QObject::tr("Deleted (unstaged)");
            case GitFileStatus::Conflicted:
                return QObject::tr("Conflicted");
            case GitFileStatus::Ignored:
                return QObject::tr("Ignored");
        }
        return {};
    }
}

FileListModel::FileListModel(const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource, QObject* parent)
    : QAbstractTableModel(parent)
    , m_showHiddenFiles(showHiddenFilesEnabled())
    , m_fileDecorationRules(fileDecorationRules)
{
    connect(&fileDecorationsChangeSource, SIGNAL(rulesChanged()), this, SLOT(handleFileDecorationsChanged()));
}

bool FileListModel::showHiddenFilesEnabled()
{
    QSettings settings;
    return settings.value(QLatin1String(kShowHiddenFilesSettingsKey), false).toBool();
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visibleRows.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(ColumnCount);
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_visibleRows.size())
    {
        return {};
    }

    const FileNode& entry = m_entries[static_cast<size_t>(m_visibleRows[static_cast<size_t>(index.row())])];

    if (role == FilePathRole)
    {
        return toQString(entry.path());
    }
    if (role == IsDirectoryRole)
    {
        return entry.isDirectory();
    }
    if (role == SizeBytesRole)
    {
        const auto size = knownSizeOf(entry);
        return size ? QVariant(static_cast<qulonglong>(*size)) : QVariant();
    }

    if (role == PercentRole)
    {
        const auto size = knownSizeOf(entry);
        if (!size || m_totalKnownSize == 0)
        {
            return QVariant();
        }
        return QVariant(100.0 * static_cast<double>(*size) / static_cast<double>(m_totalKnownSize));
    }

    if (role == Qt::DecorationRole && index.column() == NameColumn)
    {
        const QIcon icon = resolveIcon(entry);

        if (!entry.isHidden())
        {
            return icon;
        }

        // Cache the dimmed variant per icon "kind" instead of rebuilding it on every data() call --
        // all entries of the same kind (extension/directory/symlink) share the same base icon, so
        // this collapses the per-row/per-repaint cost down to one generatedIconPixmap() per kind.
        QString cacheKey = archiveSynthesizedDir(entry) ? QStringLiteral("@archive-dir")
                            : entry.isDirectory()         ? QStringLiteral("@dir")
                                                           : toQString(entry.path().extension()).toLower();
        if (cacheKey.isEmpty())
        {
            cacheKey = QStringLiteral("@file");
        }
        if (entry.fileType() == FileType::Symlink)
        {
            cacheKey.prepend(QStringLiteral("@symlink:"));
        }

        auto cached = m_dimmedIconCache.constFind(cacheKey);
        if (cached == m_dimmedIconCache.constEnd())
        {
            cached = m_dimmedIconCache.insert(cacheKey, dimmedIcon(icon));
        }
        return cached.value();
    }

    if (index.column() == GitStatusColumn && (role == Qt::DecorationRole || role == Qt::ToolTipRole))
    {
        if (entry.displayName())
        {
            // Synthetic rows ("This PC"'s drives, ...) have no real on-disk path to look up.
            return {};
        }

        const auto statusIt = m_gitStatusByPath.find(entry.path());
        if (statusIt == m_gitStatusByPath.end())
        {
            return {};
        }

        return role == Qt::DecorationRole ? QVariant(gitStatusBadgeIcon(statusIt->second)) : QVariant(gitStatusTooltip(statusIt->second));
    }

    if (role == MediaThumbnailRole && index.column() == NameColumn)
    {
        const auto thumbnailIt = m_mediaThumbnailsByPath.constFind(toQString(entry.path()));
        if (thumbnailIt == m_mediaThumbnailsByPath.constEnd())
        {
            return {};
        }
        return QVariant::fromValue(thumbnailIt.value());
    }

    if (index.column() == TagsColumn && role == TagsRole)
    {
        if (entry.displayName())
        {
            // Synthetic rows ("This PC"'s drives, ...) have no real on-disk path to look up.
            return QVariant::fromValue(std::vector<Tag>{});
        }

        const auto tagsIt = m_fileTagsByPath.find(entry.path());
        return QVariant::fromValue(tagsIt == m_fileTagsByPath.end() ? std::vector<Tag>{} : tagsIt->second);
    }

    if (role == Qt::ForegroundRole)
    {
        if (const auto decoration = resolveDecoration(entry); decoration && decoration->hexColor())
        {
            return QColor(QString::fromStdString(*decoration->hexColor()));
        }
        return {};
    }

    if (role == Qt::FontRole)
    {
        const auto decoration = resolveDecoration(entry);
        if (!decoration || (!decoration->fontFamily() && !decoration->fontPointSize() && !decoration->bold() &&
                             !decoration->italic() && !decoration->underline() && !decoration->strikeout()))
        {
            return {};
        }

        QFont font;
        if (decoration->fontFamily())
        {
            font.setFamily(QString::fromStdString(*decoration->fontFamily()));
        }
        if (decoration->fontPointSize())
        {
            font.setPointSize(*decoration->fontPointSize());
        }
        font.setBold(decoration->bold());
        font.setItalic(decoration->italic());
        font.setUnderline(decoration->underline());
        font.setStrikeOut(decoration->strikeout());
        return font;
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
        case NameColumn:
            return entry.displayName() ? QString::fromStdString(*entry.displayName()) : toQString(entry.name());
        case SizeColumn:
        {
            if (!entry.isDirectory())
            {
                return QVariant(FileSizeFormat::format(entry.size()));
            }
            const auto cachedSize = m_folderSizesByPath.find(entry.path());
            return cachedSize == m_folderSizesByPath.end() ? QVariant() : QVariant(FileSizeFormat::format(cachedSize->second));
        }
        case TypeColumn:
            return fileTypeLabel(entry);
        case DateModifiedColumn:
            return formatDate(entry.modificationDate());
        case DateCreatedColumn:
            return formatDate(entry.creationDate());
        case PercentColumn:
        {
            const auto size = knownSizeOf(entry);
            if (!size || m_totalKnownSize == 0)
            {
                return QVariant();
            }
            const int percent = static_cast<int>(100.0 * static_cast<double>(*size) / static_cast<double>(m_totalKnownSize) + 0.5);
            return QVariant(QStringLiteral("%1%").arg(percent));
        }
        case FragmentsColumn:
        {
            if (entry.isDirectory())
            {
                return QVariant();
            }
            const auto countIt = m_fragmentCountsByPath.find(entry.path());
            return countIt == m_fragmentCountsByPath.end() ? QVariant() : QVariant(static_cast<qulonglong>(countIt->second));
        }
        default:
            return {};
    }
}

Qt::ItemFlags FileListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (!index.isValid())
    {
        return result;
    }

    if (index.column() == NameColumn)
    {
        const auto entry = entryAt(index.row());
        if (entry && !entry->displayName())
        {
            result |= Qt::ItemIsEditable;
        }
    }

    return result;
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
        case GitStatusColumn:
            return tr("Git");
        case TagsColumn:
            return tr("Tags");
        case DateCreatedColumn:
            return tr("Date created");
        case PercentColumn:
            return tr("Percent");
        case FragmentsColumn:
            return tr("Fragments");
        default:
            return {};
    }
}

void FileListModel::sort(int column, Qt::SortOrder order)
{
    setSortCriterion(criterionForColumn(column), order == Qt::AscendingOrder);
}

int FileListModel::columnForCriterion(SortCriterion criterion)
{
    return ::columnForCriterion(criterion);
}

std::optional<FileNode> FileListModel::entryAt(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= m_visibleRows.size())
    {
        return std::nullopt;
    }

    return m_entries[static_cast<size_t>(m_visibleRows[static_cast<size_t>(row)])];
}

void FileListModel::setEntries(const std::filesystem::path& directory, const std::vector<FileNode>& entries)
{
    beginResetModel();
    m_directory = directory;
    m_entries = FileNavigationUseCase::sortBy(entries, m_sortCriterion, m_sortAscending,
                                                [this](const FileNode& entry) { return knownSizeOf(entry).value_or(0); });
    // A new directory's git status/tags/cached folder sizes haven't been (re)computed yet -- start
    // blank rather than momentarily showing the previous directory's badges/pills/sizes under the
    // new listing.
    m_gitStatusByPath.clear();
    m_fileTagsByPath.clear();
    m_folderSizesByPath.clear();
    m_fragmentCountsByPath.clear();
    m_iconCache.clear();
    m_mediaThumbnailsByPath.clear();
    recomputeTotalSize();
    rebuildVisibleRows();
    endResetModel();
}

void FileListModel::setSortCriterion(SortCriterion criterion, bool ascending)
{
    if (criterion == m_sortCriterion && ascending == m_sortAscending)
    {
        return;
    }

    beginResetModel();
    m_sortCriterion = criterion;
    m_sortAscending = ascending;
    m_entries = FileNavigationUseCase::sortBy(std::move(m_entries), m_sortCriterion, m_sortAscending,
                                                [this](const FileNode& entry) { return knownSizeOf(entry).value_or(0); });
    rebuildVisibleRows();
    endResetModel();

    emit sortOrderChanged(m_sortCriterion, m_sortAscending);
}

void FileListModel::setShowHiddenFiles(bool show)
{
    if (show == m_showHiddenFiles)
    {
        return;
    }

    beginResetModel();
    m_showHiddenFiles = show;
    rebuildVisibleRows();
    endResetModel();
}

void FileListModel::setGitStatuses(const GitStatusByPathMap& statuses)
{
    m_gitStatusByPath = statuses;

    if (m_visibleRows.empty())
    {
        return;
    }

    emit dataChanged(index(0, GitStatusColumn), index(static_cast<int>(m_visibleRows.size()) - 1, GitStatusColumn),
                      { Qt::DecorationRole, Qt::ToolTipRole });
}

void FileListModel::setFileTags(const TagsByPathMap& tagsByPath)
{
    m_fileTagsByPath = tagsByPath;

    if (m_visibleRows.empty())
    {
        return;
    }

    emit dataChanged(index(0, TagsColumn), index(static_cast<int>(m_visibleRows.size()) - 1, TagsColumn), { TagsRole });
}

void FileListModel::setFolderSizes(const FolderSizesByPathMap& sizesByPath)
{
    m_folderSizesByPath = sizesByPath;

    // Folder sizes normally arrive asynchronously after the initial listing/sort (Architecture.md
    // §14.39's DiskUsageService lookup runs on a worker thread) -- if rows are currently ordered by
    // Size/Percent, that earlier sort had no known directory sizes to work with (every directory
    // tied at 0), so it must be redone now that they're known, not just redisplayed.
    if (!m_entries.empty() && (m_sortCriterion == SortCriterion::Size || m_sortCriterion == SortCriterion::Percent))
    {
        beginResetModel();
        recomputeTotalSize();
        m_entries = FileNavigationUseCase::sortBy(std::move(m_entries), m_sortCriterion, m_sortAscending,
                                                    [this](const FileNode& entry) { return knownSizeOf(entry).value_or(0); });
        rebuildVisibleRows();
        endResetModel();
        return;
    }

    recomputeTotalSize();

    if (m_visibleRows.empty())
    {
        return;
    }

    emit dataChanged(index(0, SizeColumn), index(static_cast<int>(m_visibleRows.size()) - 1, SizeColumn),
                      { Qt::DisplayRole, SizeBytesRole });

    // Every row's percentage shifts when the denominator changes, not just the directory rows
    // whose own size just became known -- so this is a separate full-column emit, not folded into
    // the SizeColumn one above.
    emit dataChanged(index(0, PercentColumn), index(static_cast<int>(m_visibleRows.size()) - 1, PercentColumn),
                      { Qt::DisplayRole, PercentRole });
}

void FileListModel::setFragmentCounts(const FragmentCountsByPathMap& countsByPath)
{
    m_fragmentCountsByPath = countsByPath;

    if (m_visibleRows.empty())
    {
        return;
    }

    emit dataChanged(index(0, FragmentsColumn), index(static_cast<int>(m_visibleRows.size()) - 1, FragmentsColumn),
                      { Qt::DisplayRole });
}

void FileListModel::setIcons(const QHash<QString, QIcon>& iconsByPath)
{
    int minRow = -1;
    int maxRow = -1;

    for (auto it = iconsByPath.constBegin(); it != iconsByPath.constEnd(); ++it)
    {
        m_iconCache.insert(it.key(), it.value());

        // A key absent from m_rowByPath is either filtered out by the hidden-files toggle or a
        // stale reply for a listing this model has since moved on from (defense in depth alongside
        // the path-is-still-current check TabViewModel::requestIcons already applies) -- either
        // way, nothing visible to repaint for it.
        const auto rowIt = m_rowByPath.constFind(it.key());
        if (rowIt == m_rowByPath.constEnd())
        {
            continue;
        }

        const int row = rowIt.value();
        minRow = (minRow < 0) ? row : std::min(minRow, row);
        maxRow = std::max(maxRow, row);
    }

    if (minRow < 0)
    {
        return;
    }

    emit dataChanged(index(minRow, NameColumn), index(maxRow, NameColumn), { Qt::DecorationRole });
}

void FileListModel::setMediaThumbnails(const QHash<QString, MediaThumbnailData>& thumbnailsByPath)
{
    int minRow = -1;
    int maxRow = -1;

    for (auto it = thumbnailsByPath.constBegin(); it != thumbnailsByPath.constEnd(); ++it)
    {
        m_mediaThumbnailsByPath.insert(it.key(), it.value());

        // A key absent from m_rowByPath is either filtered out by the hidden-files toggle or a
        // stale reply for a listing this model has since moved on from -- same defense-in-depth
        // posture as setIcons().
        const auto rowIt = m_rowByPath.constFind(it.key());
        if (rowIt == m_rowByPath.constEnd())
        {
            continue;
        }

        const int row = rowIt.value();
        minRow = (minRow < 0) ? row : std::min(minRow, row);
        maxRow = std::max(maxRow, row);
    }

    if (minRow < 0)
    {
        return;
    }

    emit dataChanged(index(minRow, NameColumn), index(maxRow, NameColumn), { MediaThumbnailRole });
}

std::optional<FileDecorationRule> FileListModel::resolveDecoration(const FileNode& entry) const
{
    if (entry.displayName())
    {
        // Synthetic rows ("This PC"'s drives, quick-access folders, ...) have no real on-disk
        // filename to match against (Architecture.md §14.29).
        return std::nullopt;
    }

    if (const FileDecorationRule* rule = m_fileDecorationRules.resolve(PathUtf8::toUtf8(entry.name()), entry.isDirectory()))
    {
        return *rule;
    }
    if (entry.isHidden())
    {
        return m_fileDecorationRules.hiddenStyle(entry.isDirectory());
    }
    return std::nullopt;
}

void FileListModel::handleFileDecorationsChanged()
{
    if (m_visibleRows.empty())
    {
        return;
    }

    emit dataChanged(index(0, 0), index(static_cast<int>(m_visibleRows.size()) - 1, ColumnCount - 1),
                      { Qt::ForegroundRole, Qt::FontRole });
}

QIcon FileListModel::resolveIcon(const FileNode& entry) const
{
    const QString key = toQString(entry.path());

    const auto cached = m_iconCache.constFind(key);
    if (cached != m_iconCache.constEnd())
    {
        return cached.value();
    }

    // Not resolved by IconLoadingService yet -- a cheap generic placeholder (a static resource
    // lookup, not the expensive per-file SHGetFileInfo extraction QFileIconProvider::icon(QFileInfo)
    // does for .exe/.dll/.lnk) rather than blocking here; setIcons() overwrites this entry with the
    // real icon once the background worker delivers it.
    return m_iconProvider.icon(entry.isDirectory() ? QFileIconProvider::Folder : QFileIconProvider::File);
}

std::optional<std::uintmax_t> FileListModel::knownSizeOf(const FileNode& entry) const
{
    if (!entry.isDirectory())
    {
        return entry.size();
    }

    const auto cachedSize = m_folderSizesByPath.find(entry.path());
    if (cachedSize == m_folderSizesByPath.end())
    {
        return std::nullopt;
    }
    return cachedSize->second;
}

void FileListModel::recomputeTotalSize()
{
    m_totalKnownSize = 0;
    for (const FileNode& entry : m_entries)
    {
        if (const auto size = knownSizeOf(entry))
        {
            m_totalKnownSize += *size;
        }
    }
}

void FileListModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    m_visibleRows.reserve(m_entries.size());
    m_rowByPath.clear();
    m_rowByPath.reserve(static_cast<int>(m_entries.size()));
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
    {
        if (m_showHiddenFiles || !m_entries[static_cast<size_t>(i)].isHidden())
        {
            m_rowByPath.insert(toQString(m_entries[static_cast<size_t>(i)].path()), static_cast<int>(m_visibleRows.size()));
            m_visibleRows.push_back(i);
        }
    }
}
