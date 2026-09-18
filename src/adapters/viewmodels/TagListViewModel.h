#pragma once

#include <optional>
#include <vector>

#include <QObject>
#include <QString>

#include "FileNode.h"
#include "Tag.h"

class TagManagementUseCase;
class FileNavigationUseCase;
class TabViewModel;

// The single shared ViewModel behind the whole tag panel (Architecture.md §14.9), retargeted by
// WorkspaceController to whichever pane/tab currently has focus. Untested per the codebase's
// existing UI/ViewModel convention (Architecture.md §11, same as WorkspaceController).
class TagListViewModel : public QObject
{
    Q_OBJECT

public:
    TagListViewModel(TagManagementUseCase& tagManagementUseCase, FileNavigationUseCase& fileNavigationUseCase,
                      QObject* parent = nullptr);

    // Requirement 1.1: tags of the active tab's current folder plus every ancestor's tags,
    // outermost ancestor first.
    const std::vector<Tag>& folderTags() const noexcept { return m_folderTags; }

    // Requirement 1.3: tags matching the current search query, excluding tags already applied to
    // the resolved target (so a visible "+" never triggers AlreadyExists); alphabetical when the
    // query is empty.
    const std::vector<Tag>& searchResults() const noexcept { return m_searchResults; }

    // Requirement 1.4: tags of only the resolved target (selected item, or the browsed folder
    // itself when nothing is selected) — no ancestors.
    const std::vector<Tag>& selectedItemTags() const noexcept { return m_selectedItemTags; }

    // Whether the current search query has no existing exact (case-insensitive) name match, so
    // the panel's inline "Create tag "<query>"" affordance should be shown.
    bool canCreateTagFromQuery() const noexcept { return m_canCreateTagFromQuery; }

    const QString& searchQuery() const noexcept { return m_searchQuery; }

public slots:
    // Called by WorkspaceController's retargeting hook (focusedPaneChanged / any pane's
    // activeTabChanged). Disconnects the previous tab, connects the new one, and refreshes all
    // three sections immediately.
    void setActiveTab(TabViewModel* tab);

    void setSearchQuery(const QString& query);

    // Resolves the tagging target via resolveTarget() and assigns/unassigns tagId to/from it.
    void addTagToSelection(Tag::Id tagId);
    void removeTagFromSelection(Tag::Id tagId);

    // Creates a new tag named by the trimmed current search query (color auto-picked from
    // TagColorPalette) and immediately assigns it to the resolved target.
    void createAndAddTagFromQuery();

    // Requirement 1/2: opens Advanced Search on the focused tab with tagId as a criterion. No-op
    // if there is no active/focused tab.
    void requestTagSearch(Tag::Id tagId);

signals:
    void folderTagsChanged(const std::vector<Tag>& tags);
    void searchResultsChanged(const std::vector<Tag>& tags);
    void selectedItemTagsChanged(const std::vector<Tag>& tags);
    void canCreateTagFromQueryChanged(bool canCreate);
    void operationFailed(const QString& message);

private:
    void onActiveTabStateChanged();
    std::optional<FileNode> resolveTarget() const;

    void refreshFolderTags();
    void refreshSelectedItemTags();
    void refreshSearchSection();
    void refreshAll();

    TagManagementUseCase& m_tagManagementUseCase;
    FileNavigationUseCase& m_fileNavigationUseCase;
    TabViewModel* m_activeTab = nullptr;

    QString m_searchQuery;
    std::vector<Tag> m_folderTags;
    std::vector<Tag> m_searchResults;
    std::vector<Tag> m_selectedItemTags;
    bool m_canCreateTagFromQuery = false;
};
