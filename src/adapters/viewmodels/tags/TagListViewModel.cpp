#include "TagListViewModel.h"

#include <algorithm>
#include <cctype>

#include "ArchiveExtensions.h"
#include "FileNavigationUseCase.h"
#include "FileTagsByPath.h"
#include "NotificationCenter.h"
#include "NotificationType.h"
#include "TabViewModel.h"
#include "TagChangeNotifier.h"
#include "TagColorPalette.h"
#include "TagManagementUseCase.h"
#include "VirtualPaths.h"

namespace
{
    bool containsTagId(const std::vector<Tag>& tags, Tag::Id id)
    {
        return std::any_of(tags.begin(), tags.end(), [id](const Tag& tag) { return tag.id() == id; });
    }

    const Tag* findTagId(const std::vector<Tag>& tags, Tag::Id id)
    {
        const auto it = std::find_if(tags.begin(), tags.end(), [id](const Tag& tag) { return tag.id() == id; });
        return it == tags.end() ? nullptr : &(*it);
    }
}

TagListViewModel::TagListViewModel(TagManagementUseCase& tagManagementUseCase, FileNavigationUseCase& fileNavigationUseCase,
                                    NotificationCenter& notificationCenter, TagChangeNotifier& tagChangeNotifier, QObject* parent)
    : QObject(parent)
    , m_tagManagementUseCase(tagManagementUseCase)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_notificationCenter(notificationCenter)
    , m_tagChangeNotifier(tagChangeNotifier)
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
        disconnect(m_activeTab, &TabViewModel::selectedEntriesChanged, this, &TagListViewModel::onActiveTabStateChanged);
    }

    m_activeTab = tab;

    if (m_activeTab)
    {
        connect(m_activeTab, &TabViewModel::currentPathChanged, this, &TagListViewModel::onActiveTabStateChanged);
        connect(m_activeTab, &TabViewModel::selectedEntriesChanged, this, &TagListViewModel::onActiveTabStateChanged);
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
    const std::vector<FileNode> targets = resolveTargets();
    if (targets.empty())
    {
        emit operationFailed(tr("No folder or file to tag."));
        return;
    }

    const QString tagName = resolveTagName(tagId);

    std::size_t successCount = 0;
    QString lastErrorMessage;
    for (const FileNode& target : targets)
    {
        auto result = m_tagManagementUseCase.assignTag(target, tagId);
        if (result || result.error().code == ErrorCode::AlreadyExists)
        {
            ++successCount;
        }
        else
        {
            lastErrorMessage = QString::fromStdString(result.error().message);
        }
    }

    if (successCount == 0)
    {
        emit operationFailed(lastErrorMessage);
        return;
    }

    if (targets.size() == 1)
    {
        const FileNode& target = targets.front();
        const QString targetName = target.displayName() ? QString::fromStdString(*target.displayName())
                                                          : QString::fromStdWString(target.name().wstring());
        m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Applied"),
                                        tr("\"%1\" tagged with \"%2\".").arg(targetName, tagName));
    }
    else
    {
        m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Applied"),
                                        tr("%1 item(s) tagged with \"%2\".").arg(successCount).arg(tagName));
    }

    refreshAll();
    emit m_tagChangeNotifier.tagsChanged();
}

void TagListViewModel::removeTagFromSelection(Tag::Id tagId)
{
    const std::vector<FileNode> targets = resolveTargets();
    if (targets.empty())
    {
        emit operationFailed(tr("No folder or file to untag."));
        return;
    }

    const QString tagName = resolveTagName(tagId);

    std::size_t successCount = 0;
    QString lastErrorMessage;
    for (const FileNode& target : targets)
    {
        auto result = m_tagManagementUseCase.unassignTag(target, tagId);
        if (result || result.error().code == ErrorCode::NotFound)
        {
            ++successCount;
        }
        else
        {
            lastErrorMessage = QString::fromStdString(result.error().message);
        }
    }

    if (successCount == 0)
    {
        emit operationFailed(lastErrorMessage);
        return;
    }

    if (targets.size() == 1)
    {
        const FileNode& target = targets.front();
        const QString targetName = target.displayName() ? QString::fromStdString(*target.displayName())
                                                          : QString::fromStdWString(target.name().wstring());
        m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Removed"),
                                        tr("\"%1\" untagged from \"%2\".").arg(targetName, tagName));
    }
    else
    {
        m_notificationCenter.showPopup(NotificationType::Info, tr("Tag Removed"),
                                        tr("\"%1\" removed from %2 item(s).").arg(tagName).arg(successCount));
    }

    refreshAll();
    emit m_tagChangeNotifier.tagsChanged();
}

void TagListViewModel::setSelectedItemTagsCombineMode(CombineMode mode)
{
    if (m_selectedItemTagsCombineMode == mode)
    {
        return;
    }

    m_selectedItemTagsCombineMode = mode;
    refreshSelectedItemTags();
    refreshSearchSection();
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

void TagListViewModel::acceptSearchQuery()
{
    const QString trimmed = m_searchQuery.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    if (m_canCreateTagFromQuery)
    {
        createAndAddTagFromQuery();
        return;
    }

    const auto it = std::find_if(m_searchResults.begin(), m_searchResults.end(), [&trimmed](const Tag& tag) {
        return QString::fromStdString(tag.name()).compare(trimmed, Qt::CaseInsensitive) == 0;
    });
    if (it != m_searchResults.end())
    {
        addTagToSelection(it->id());
    }
}

void TagListViewModel::requestTagSearch(Tag::Id tagId)
{
    if (m_activeTab)
    {
        m_activeTab->addTagSearchCriterion(tagId);
    }
}

void TagListViewModel::onActiveTabStateChanged()
{
    refreshAll();
}

std::vector<FileNode> TagListViewModel::resolveTargets() const
{
    if (!m_activeTab)
    {
        return {};
    }

    std::vector<FileNode> candidates;

    const std::vector<FileNode>& selected = m_activeTab->selectedEntries();
    if (selected.empty())
    {
        if (m_activeTab->currentPath() == VirtualPaths::ThisPC)
        {
            return {};
        }

        auto stat = m_fileNavigationUseCase.stat(m_activeTab->currentPath());
        if (!stat)
        {
            return {};
        }
        candidates.push_back(std::move(stat).value());
    }
    else
    {
        candidates = selected;
    }

    // Tagging inside an archive is disabled (Architecture.md §14.28): computeFileHash's
    // rename/move-relocation fallback can't be computed for a virtual entry without a full
    // extraction. Each archive-nested entry is simply dropped from the target list, rather than
    // blocking tagging of the rest of a multi-selection.
    std::vector<FileNode> targets;
    targets.reserve(candidates.size());
    for (FileNode& candidate : candidates)
    {
        if (!ArchiveExtensions::archiveAncestorInPath(candidate.path()))
        {
            targets.push_back(std::move(candidate));
        }
    }

    return targets;
}

QString TagListViewModel::resolveTagName(Tag::Id tagId) const
{
    if (const Tag* tag = findTagId(m_searchResults, tagId))
    {
        return QString::fromStdString(tag->name());
    }
    if (const Tag* tag = findTagId(m_folderTags, tagId))
    {
        return QString::fromStdString(tag->name());
    }
    if (const Tag* tag = findTagId(m_selectedItemTags, tagId))
    {
        return QString::fromStdString(tag->name());
    }

    auto all = m_tagManagementUseCase.allTags();
    if (all)
    {
        if (const Tag* tag = findTagId(all.value(), tagId))
        {
            return QString::fromStdString(tag->name());
        }
    }

    return QString();
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

    const std::vector<FileNode> targets = resolveTargets();
    if (targets.size() == 1)
    {
        auto result = m_tagManagementUseCase.tagsForFile(targets.front());
        if (result)
        {
            m_selectedItemTags = std::move(result).value();
        }
        else
        {
            emit operationFailed(QString::fromStdString(result.error().message));
        }
    }
    else if (targets.size() > 1)
    {
        // Requirement 1.4 extended to a multi-selection: the tags shown (and therefore
        // actionable in one action) are combined per m_selectedItemTagsCombineMode --
        // CombineMode::And intersects (tags common to every selected item), CombineMode::Or
        // unions (every tag present on at least one target).
        auto result = m_tagManagementUseCase.tagsForFiles(targets);
        if (result)
        {
            const TagsByPathMap& tagsByPath = result.value();

            if (m_selectedItemTagsCombineMode == CombineMode::And)
            {
                bool first = true;
                for (const FileNode& target : targets)
                {
                    const auto it = tagsByPath.find(target.path());
                    const std::vector<Tag>& tagsForTarget = (it != tagsByPath.end()) ? it->second : std::vector<Tag>{};

                    if (first)
                    {
                        m_selectedItemTags = tagsForTarget;
                        first = false;
                        continue;
                    }

                    std::vector<Tag> intersected;
                    for (const Tag& tag : m_selectedItemTags)
                    {
                        if (containsTagId(tagsForTarget, tag.id()))
                        {
                            intersected.push_back(tag);
                        }
                    }
                    m_selectedItemTags = std::move(intersected);

                    if (m_selectedItemTags.empty())
                    {
                        break;
                    }
                }
            }
            else
            {
                for (const FileNode& target : targets)
                {
                    const auto it = tagsByPath.find(target.path());
                    const std::vector<Tag>& tagsForTarget = (it != tagsByPath.end()) ? it->second : std::vector<Tag>{};

                    for (const Tag& tag : tagsForTarget)
                    {
                        if (!containsTagId(m_selectedItemTags, tag.id()))
                        {
                            m_selectedItemTags.push_back(tag);
                        }
                    }
                }
            }
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
