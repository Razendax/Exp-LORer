#include "TagManagerViewModel.h"

#include <algorithm>
#include <utility>

#include "AdvancedSearchService.h"
#include "FileTagsService.h"
#include "IconLoadingService.h"
#include "IconSizeMask.h"
#include "TagChangeNotifier.h"
#include "TagColorPalette.h"
#include "TagManagementUseCase.h"

TagManagerViewModel::TagManagerViewModel(TagManagementUseCase& tagManagementUseCase, TagChangeNotifier& tagChangeNotifier,
                                          AdvancedSearchService& advancedSearchService, IconLoadingService& iconLoadingService,
                                          FileTagsService& fileTagsService, QObject* parent)
    : QObject(parent)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_tagChangeNotifier(tagChangeNotifier)
    , m_advancedSearchService(advancedSearchService)
    , m_iconLoadingService(iconLoadingService)
    , m_fileTagsService(fileTagsService)
{
    refreshSearch();
}

std::vector<Tag> TagManagerViewModel::allTags() const
{
    auto result = m_tagManagementUseCase.allTags();
    return result ? std::move(result).value() : std::vector<Tag>{};
}

int TagManagerViewModel::countForTag(Tag::Id id) const
{
    const auto it = m_tagCounts.find(id);
    return it != m_tagCounts.end() ? it->second : 0;
}

void TagManagerViewModel::setSearchQuery(const QString& query)
{
    if (m_searchQuery == query)
    {
        return;
    }

    m_searchQuery = query;
    refreshSearch();
}

void TagManagerViewModel::renameTag(Tag::Id id, const QString& newName)
{
    const auto existing = std::find_if(m_matchingTags.begin(), m_matchingTags.end(),
                                        [id](const Tag& tag) { return tag.id() == id; });
    if (existing == m_matchingTags.end())
    {
        emit operationFailed(tr("Tag no longer exists."));
        return;
    }

    auto rebuilt = Tag::create(id, newName.toStdString(), existing->hexColor());
    if (!rebuilt)
    {
        emit operationFailed(QString::fromStdString(rebuilt.error().message));
        return;
    }

    auto result = m_tagManagementUseCase.updateTag(rebuilt.value());
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    refreshSearch();
    if (m_showCounts)
    {
        refreshCounts();
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TagManagerViewModel::addTag(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    const std::string colorHex = TagColorPalette::pickColorFor(trimmed.toStdString());
    auto created = m_tagManagementUseCase.createTag(trimmed.toStdString(), colorHex);
    if (!created)
    {
        emit operationFailed(QString::fromStdString(created.error().message));
        return;
    }

    refreshSearch();
    if (m_showCounts)
    {
        refreshCounts();
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TagManagerViewModel::deleteTag(Tag::Id id)
{
    auto result = m_tagManagementUseCase.deleteTag(id);
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    refreshSearch();
    if (m_showCounts)
    {
        refreshCounts();
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TagManagerViewModel::mergeTags(Tag::Id sourceId, Tag::Id targetId)
{
    auto result = m_tagManagementUseCase.mergeTags(sourceId, targetId);
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    refreshSearch();
    if (m_showCounts)
    {
        refreshCounts();
    }
    emit m_tagChangeNotifier.tagsChanged();
}

void TagManagerViewModel::setShowCounts(bool show)
{
    m_showCounts = show;
    if (show)
    {
        refreshCounts();
    }

    emit matchingTagsChanged(m_matchingTags);
}

void TagManagerViewModel::setSelectedTag(std::optional<Tag::Id> id)
{
    m_selectedTagId = id;
    const int generation = ++m_filesForTagGeneration;

    if (!id.has_value())
    {
        emit filesForTagChanged({});
        return;
    }

    m_advancedSearchService.requestFilesForTag(this, *id, [this, generation](Result<std::vector<FileNode>> result) {
        if (generation != m_filesForTagGeneration)
        {
            // Superseded by a newer chip selection -- discard, same discard-by-generation idiom
            // TabViewModel uses throughout.
            return;
        }

        const std::vector<FileNode> files = result ? result.value() : std::vector<FileNode>{};
        emit filesForTagChanged(files);

        // No single root directory for this request (files can come from anywhere the tag was
        // applied), so an empty path is passed -- our own generation check above covers staleness,
        // so IconLoadingService's own internal per-context directory check doesn't need to do
        // anything useful here.
        m_iconLoadingService.requestIcons(this, {}, files, IconSizeMask::Small,
                                           [this, generation](QHash<QString, QIcon> batch) {
                                               if (generation != m_filesForTagGeneration)
                                               {
                                                   return;
                                               }
                                               emit filesForTagIconsChanged(batch);
                                           });

        m_fileTagsService.requestTags(this, files, [this, generation](Result<TagsByPathMap> tagsResult) {
            if (generation != m_filesForTagGeneration)
            {
                return;
            }
            emit filesForTagTagsChanged(tagsResult ? tagsResult.value() : TagsByPathMap{});
        });
    });
}

void TagManagerViewModel::refreshCounts()
{
    auto result = m_tagManagementUseCase.tagFileCounts();
    m_tagCounts = result ? std::move(result).value() : std::unordered_map<Tag::Id, int>{};
}

void TagManagerViewModel::refreshSearch()
{
    auto result = m_tagManagementUseCase.searchTags(m_searchQuery.trimmed().toStdString());
    m_matchingTags.clear();
    if (result)
    {
        m_matchingTags = std::move(result).value();
    }
    else
    {
        emit operationFailed(QString::fromStdString(result.error().message));
    }

    emit matchingTagsChanged(m_matchingTags);
}
