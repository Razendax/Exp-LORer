#include "TagManagerViewModel.h"

#include <algorithm>
#include <utility>

#include "TagColorPalette.h"
#include "TagManagementUseCase.h"

TagManagerViewModel::TagManagerViewModel(TagManagementUseCase& tagManagementUseCase, QObject* parent)
    : QObject(parent)
    , m_tagManagementUseCase(tagManagementUseCase)
{
    refreshSearch();
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
