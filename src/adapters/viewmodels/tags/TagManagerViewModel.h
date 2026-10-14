#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

#include "FileNode.h"
#include "FileTagsByPath.h"
#include "Tag.h"

class TagManagementUseCase;
class TagChangeNotifier;
class AdvancedSearchService;
class IconLoadingService;
class FileTagsService;

// Backs the standalone Tag Manager dialog (Architecture.md §14.17), opened via Edit > "Tag
// Edit..." to manage the global tag list (rename/add/delete) independently of any specific
// file/folder selection. Untested per the codebase's existing UI/ViewModel convention
// (Architecture.md §11, same as TagListViewModel).
class TagManagerViewModel : public QObject
{
    Q_OBJECT

public:
    // Architecture.md §14.92.
    enum class SortKey { Alphabetical, Count };
    enum class SortOrder { Ascending, Descending };

    TagManagerViewModel(TagManagementUseCase& tagManagementUseCase, TagChangeNotifier& tagChangeNotifier,
                         AdvancedSearchService& advancedSearchService, IconLoadingService& iconLoadingService,
                         FileTagsService& fileTagsService, QObject* parent = nullptr);

    const std::vector<Tag>& matchingTags() const noexcept { return m_matchingTags; }

    // Architecture.md §14.89: a global exact-name lookup independent of the current search filter
    // (m_matchingTags), used by the dialog's rename-into-existing-name collision check.
    std::vector<Tag> allTags() const;

    bool showCounts() const noexcept { return m_showCounts; }
    SortKey sortKey() const noexcept { return m_sortKey; }
    SortOrder sortOrder() const noexcept { return m_sortOrder; }

    // Counts are needed (and kept fresh) whenever they're displayed OR used as the sort key
    // (Architecture.md §14.92).
    bool countsNeeded() const noexcept { return m_showCounts || m_sortKey == SortKey::Count; }

    // 0 if `id` has no entry (no files tagged with it, or counts not currently shown/needed).
    int countForTag(Tag::Id id) const;

public slots:
    void setSearchQuery(const QString& query);

    // Renames a tag in place (color untouched) via Tag::create(id, newName, existingColor) +
    // TagManagementUseCase::updateTag.
    void renameTag(Tag::Id id, const QString& newName);

    // Creates a new tag with a color auto-picked from TagColorPalette.
    void addTag(const QString& name);

    void deleteTag(Tag::Id id);

    // Folds sourceId into targetId (Architecture.md §14.89) -- the Tag Manager dialog's
    // rename-into-an-existing-name "merge?" prompt. On success, refreshes the search/count state
    // the same way renameTag/deleteTag do.
    void mergeTags(Tag::Id sourceId, Tag::Id targetId);

    // Toggles the per-chip file-count display; fetches counts immediately when turning on, then
    // re-emits matchingTagsChanged so the dialog rebuilds its chips with/without counts.
    void setShowCounts(bool show);

    // Architecture.md §14.92. Re-emits matchingTagsChanged so the dialog re-renders in the new
    // order; the ordering itself is computed by the dialog, m_matchingTags is left untouched.
    void setSortKey(SortKey key);
    void setSortOrder(SortOrder order);

    // Tracks which chip is selected, purely to drive the async files-for-tag fetch below --
    // independent of (and not a replacement for) the dialog's own m_selectedTagId, which still
    // governs Rename/Delete button enablement. std::nullopt clears the results list immediately.
    // Also kicks off the icon/Tags-column fetches below for the same files once they're known
    // (Architecture.md §14.90).
    void setSelectedTag(std::optional<Tag::Id> id);

signals:
    void matchingTagsChanged(const std::vector<Tag>& tags);
    void operationFailed(const QString& message);

    // Files/folders tagged with the tag last passed to setSelectedTag -- empty immediately for
    // std::nullopt, or asynchronously once the AdvancedSearchService request completes.
    void filesForTagChanged(const std::vector<FileNode>& files);

    // Icon/Tags-column batches for the files last emitted via filesForTagChanged (Architecture.md
    // §14.90) -- fetched once per setSelectedTag call via IconLoadingService/FileTagsService, same
    // "request once per selection, no live re-filter" posture filesForTagChanged itself has.
    void filesForTagIconsChanged(const QHash<QString, QIcon>& icons);
    void filesForTagTagsChanged(const TagsByPathMap& tagsByPath);

private:
    void refreshSearch();
    void refreshCounts();

    TagManagementUseCase& m_tagManagementUseCase;
    TagChangeNotifier& m_tagChangeNotifier;
    AdvancedSearchService& m_advancedSearchService;
    IconLoadingService& m_iconLoadingService;
    FileTagsService& m_fileTagsService;
    QString m_searchQuery;
    std::vector<Tag> m_matchingTags;
    bool m_showCounts = false;
    SortKey m_sortKey = SortKey::Alphabetical;
    SortOrder m_sortOrder = SortOrder::Ascending;
    std::unordered_map<Tag::Id, int> m_tagCounts;
    std::optional<Tag::Id> m_selectedTagId;

    // Incremented at the top of every setSelectedTag call; captured by the files-for-tag reply and
    // the two icon/tags requests it kicks off, so a reply whose captured value no longer matches is
    // discarded as superseded by a newer chip selection -- same discard-by-generation idiom
    // TabViewModel uses throughout (Architecture.md §14.90).
    int m_filesForTagGeneration = 0;
};
