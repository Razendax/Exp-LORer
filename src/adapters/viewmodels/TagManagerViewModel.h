#pragma once

#include <vector>

#include <QObject>
#include <QString>

#include "Tag.h"

class TagManagementUseCase;

// Backs the standalone Tag Manager dialog (Architecture.md §14.17), opened via Edit > "Tag
// Edit..." to manage the global tag list (rename/add/delete) independently of any specific
// file/folder selection. Untested per the codebase's existing UI/ViewModel convention
// (Architecture.md §11, same as TagListViewModel).
class TagManagerViewModel : public QObject
{
    Q_OBJECT

public:
    explicit TagManagerViewModel(TagManagementUseCase& tagManagementUseCase, QObject* parent = nullptr);

    const std::vector<Tag>& matchingTags() const noexcept { return m_matchingTags; }

public slots:
    void setSearchQuery(const QString& query);

    // Renames a tag in place (color untouched) via Tag::create(id, newName, existingColor) +
    // TagManagementUseCase::updateTag.
    void renameTag(Tag::Id id, const QString& newName);

    // Creates a new tag with a color auto-picked from TagColorPalette.
    void addTag(const QString& name);

    void deleteTag(Tag::Id id);

signals:
    void matchingTagsChanged(const std::vector<Tag>& tags);
    void operationFailed(const QString& message);

private:
    void refreshSearch();

    TagManagementUseCase& m_tagManagementUseCase;
    QString m_searchQuery;
    std::vector<Tag> m_matchingTags;
};
