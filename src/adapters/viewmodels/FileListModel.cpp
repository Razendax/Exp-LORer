#include "FileListModel.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QSettings>
#include <QStyle>
#include <QStyleOption>

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
            case SortCriterion::Name:
            default:
                return FileListModel::NameColumn;
        }
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

    if (role == Qt::DecorationRole && index.column() == NameColumn)
    {
        const QFileInfo info(toQString(entry.path()));
        const bool archiveSynthesizedDir = entry.isDirectory() && !info.exists();
        QIcon icon;
        if (archiveSynthesizedDir)
        {
            // Archive-synthesized directories (Architecture.md §14.28) have no real path on disk,
            // so QFileIconProvider::icon(QFileInfo) can't stat them and falls back to a generic/file
            // icon instead of a folder icon.
            icon = m_iconProvider.icon(QFileIconProvider::Folder);
        }
        else
        {
            icon = m_iconProvider.icon(info);
        }

        if (!entry.isHidden())
        {
            return icon;
        }

        // Cache the dimmed variant per icon "kind" instead of rebuilding it on every data() call --
        // all entries of the same kind (extension/directory/symlink) share the same base icon, so
        // this collapses the per-row/per-repaint cost down to one generatedIconPixmap() per kind.
        QString cacheKey = archiveSynthesizedDir  ? QStringLiteral("@archive-dir")
                            : entry.isDirectory() ? QStringLiteral("@dir")
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
            return entry.isDirectory() ? QVariant() : QVariant(static_cast<qulonglong>(entry.size()));
        case TypeColumn:
            return fileTypeLabel(entry);
        case DateModifiedColumn:
            return formatDate(entry.modificationDate());
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
    m_entries = FileNavigationUseCase::sortBy(entries, m_sortCriterion, m_sortAscending);
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
    m_entries = FileNavigationUseCase::sortBy(std::move(m_entries), m_sortCriterion, m_sortAscending);
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

void FileListModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    m_visibleRows.reserve(m_entries.size());
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
    {
        if (m_showHiddenFiles || !m_entries[static_cast<size_t>(i)].isHidden())
        {
            m_visibleRows.push_back(i);
        }
    }
}
