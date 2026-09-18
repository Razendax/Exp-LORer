#pragma once

#include <vector>

#include <QWidget>

#include "Tag.h"

class FlowLayout;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class TagListViewModel;

// The right-hand tag panel (Architecture.md §14.9, Specification.md "Tag Panel"): (1.1) a
// scrollable list of read-only chips for the active folder + its ancestors, (1.2) a search box,
// (1.3) a scrollable list of addable chips matching the search plus an inline "Create tag"
// affordance, and (1.4) a scrollable list of removable chips for the resolved selection. Rebuilds
// each section's chip list wholesale on the corresponding TagListViewModel *Changed signal.
class TagPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TagPanelWidget(TagListViewModel* viewModel, QWidget* parent = nullptr);

private:
    // Appends a titled, scrollable chip-flow section to `mainLayout` and returns the (empty)
    // layout chips get added to.
    FlowLayout* addChipSection(QVBoxLayout* mainLayout, const QString& title);

    void rebuildFolderTags(const std::vector<Tag>& tags);
    void rebuildSearchResults(const std::vector<Tag>& tags);
    void rebuildSelectedItemTags(const std::vector<Tag>& tags);
    void updateCreateTagAffordance(bool canCreate);
    void onTagChipClicked(Tag::Id tagId);
    static void clearLayout(FlowLayout* layout);

    TagListViewModel* m_viewModel = nullptr;

    FlowLayout* m_folderTagsLayout = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    FlowLayout* m_searchResultsLayout = nullptr;
    QPushButton* m_createTagButton = nullptr;
    FlowLayout* m_selectedItemTagsLayout = nullptr;
};
