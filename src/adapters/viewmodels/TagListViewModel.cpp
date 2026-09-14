#include "TagListViewModel.h"

#include <algorithm>
#include <cctype>

#include "FileNavigationUseCase.h"
#include "TabViewModel.h"
#include "TagColorPalette.h"
#include "TagManagementUseCase.h"

namespace
{
    bool containsTagId(const std::vector<Tag>& tags, Tag::Id id)
    {
        return std::any_of(tags.begin(), tags.end(), [id](const Tag& tag) { return tag.id() == id; });
    }
}

TagListViewModel::TagListViewModel(TagManagementUseCase& tagManagementUseCase, FileNavigationUseCase& fileNavigationUseCase,
                                    QObject* parent)
    : QObject(parent)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileNavigationUseCase(fileNavigationUseCase)
{
    refreshAll();
}

void TagListViewModel::setActiveTab(TabViewModel* tab)
{
    if (m_activeTab == tab)
    {
        return;
    }

    if (m_activeTab)
    {
        disconnect(m_activeTab, &TabViewModel::currentPathChanged, this, &TagListViewModel::onActiveTabStateChanged);
        disconnect(m_activeTab, &TabViewModel::selectedEntryChanged, this, &TagListViewModel::onActiveTabStateChanged);
    }

    m_activeTab = tab;

    if (m_activeTab)
    {
        connect(m_activeTab, &TabViewModel::currentPathChanged, this, &TagListViewModel::onActiveTabStateChanged);
        connect(m_activeTab, &TabViewModel::selectedEntryChanged, this, &TagListViewModel::onActiveTabStateChanged);
    }

    refreshAll();
}

void TagListViewModel::setSearchQuery(const QString& query)
{
    if (m_searchQuery == query)
    {
        return;
    }

    m_searchQuery = query;
    refreshSearchSection();
}

void TagListViewModel::addTagToSelection(Tag::Id tagId)
{
    auto target = resolveTarget();
    if (!target)
    {
        emit operationFailed(tr("No folder or file to tag."));
        return;
    }

    auto result = m_tagManagementUseCase.assignTag(*target, tagId);
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    refreshAll();
}

void TagListViewModel::removeTagFromSelection(Tag::Id tagId)
{
    auto target = resolveTarget();
    if (!target)
    {
        emit operationFailed(tr("No folder or file to untag."));
        return;
    }

    auto result = m_tagManagementUseCase.unassignTag(*target, tagId);
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    refreshAll();
}

void TagListViewModel::createAndAddTagFromQuery()
{
    const QString trimmed = m_searchQuery.trimmed();
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

    addTagToSelection(created.value().id());
}

void TagListViewModel::onActiveTabStateChanged()
{
    refreshAll();
}

std::optional<FileNode> TagListViewModel::resolveTarget() const
{
    if (!m_activeTab)
    {
        return std::nullopt;
    }

    if (const auto& selected = m_activeTab->selectedEntry())
    {
        return selected;
    }

    auto stat = m_fileNavigationUseCase.stat(m_activeTab->currentPath());
    if (!stat)
    {
        return std::nullopt;
    }

    return stat.value();
}

void TagListViewModel::refreshFolderTags()
{
    m_folderTags.clear();

    if (m_activeTab)
    {
        auto result = m_tagManagementUseCase.tagsForPathWithAncestors(m_activeTab->currentPath());
        if (result)
        {
            m_folderTags = std::move(result).value();
        }
        else
        {
            emit operationFailed(QString::fromStdString(result.error().message));
        }
    }

    emit folderTagsChanged(m_folderTags);
}

void TagListViewModel::refreshSelectedItemTags()
{
    m_selectedItemTags.clear();

    auto target = resolveTarget();
    if (target)
    {
        auto result = m_tagManagementUseCase.tagsForFile(*target);
        if (result)
        {
            m_selectedItemTags = std::move(result).value();
        }
        else
        {
            emit operationFailed(QString::fromStdString(result.error().message));
        }
    }

    emit selectedItemTagsChanged(m_selectedItemTags);
}

void TagListViewModel::refreshSearchSection()
{
    const std::string query = m_searchQuery.trimmed().toStdString();

    auto result = m_tagManagementUseCase.searchTags(query);
    std::vector<Tag> ranked;
    if (result)
    {
        ranked = std::move(result).value();
    }
    else
    {
        emit operationFailed(QString::fromStdString(result.error().message));
    }

    const bool hasExactMatch = std::any_of(ranked.begin(), ranked.end(), [&query](const Tag& tag) {
        return tag.name().size() == query.size() &&
               std::equal(tag.name().begin(), tag.name().end(), query.begin(), [](unsigned char a, unsigned char b) {
                   return std::tolower(a) == std::tolower(b);
               });
    });
    m_canCreateTagFromQuery = !query.empty() && !hasExactMatch;

    m_searchResults.clear();
    std::copy_if(ranked.begin(), ranked.end(), std::back_inserter(m_searchResults),
                 [this](const Tag& tag) { return !containsTagId(m_selectedItemTags, tag.id()); });

    emit searchResultsChanged(m_searchResults);
    emit canCreateTagFromQueryChanged(m_canCreateTagFromQuery);
}

void TagListViewModel::refreshAll()
{
    refreshFolderTags();
    refreshSelectedItemTags();
    refreshSearchSection();
}
