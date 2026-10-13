#pragma once

#include <vector>

#include <QWidget>

#include "Tag.h"
#include "UiColors.h"

class FlowLayout;
class QComboBox;
class QPushButton;
class QVBoxLayout;
class TagListViewModel;
class TagSearchEdit;
class UiThemeChangeNotifier;

// The right-hand tag panel (Architecture.md §14.9, Specification.md "Tag Panel"): (1.1) a
// scrollable list of read-only chips for the active folder + its ancestors, (1.2) a search box,
// (1.3) a scrollable list of addable chips matching the search plus an inline "Create tag"
// affordance, and (1.4) a scrollable list of removable chips for the resolved selection. Rebuilds
// each section's chip list wholesale on the corresponding TagListViewModel *Changed signal.
class TagPanelWidget : public QWidget
{
    Q_OBJECT

public:
    TagPanelWidget(TagListViewModel* viewModel, UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

private:
    // Pointer-to-member into UiColors::UiColorSet, naming which role tints a given chip section --
    // resolved against UiColors::current() both at construction and again on every themeChanged
    // (Architecture.md §14.50), rather than a literal hex baked in once.
    using ColorRole = const char* UiColors::UiColorSet::*;

    // Appends a titled, scrollable chip-flow section to `mainLayout` and returns the (empty)
    // layout chips get added to. colorRole selects which UiColorSet member tints the section so
    // the three sections read as visually distinct (UiColors.h); the section is recorded in
    // m_themedSections so applyThemeColors() can re-tint it later. sectionObjectName is the UI
    // Automation id for the section's container widget (Architecture.md §11).
    // headerExtraWidget, when non-null, is placed alongside the title label in a header row
    // (e.g. the AND/OR combo on "Tags on selection") instead of the label getting the row alone.
    FlowLayout* addChipSection(QVBoxLayout* mainLayout, const QString& title, ColorRole colorRole, const QString& sectionObjectName,
                                QWidget* headerExtraWidget = nullptr);

    void rebuildFolderTags(const std::vector<Tag>& tags);
    void rebuildSearchResults(const std::vector<Tag>& tags);
    void rebuildSelectedItemTags(const std::vector<Tag>& tags);
    void updateCreateTagAffordance(bool canCreate);
    void updateSearchSuggestions(const std::vector<Tag>& tags);
    void onTagChipClicked(Tag::Id tagId);
    void onSearchEditReturnPressed();
    static void clearLayout(FlowLayout* layout);

    // Re-applies every recorded chip section's tint from UiColors::current() (Architecture.md
    // §14.50).
    void applyThemeColors();

    TagListViewModel* m_viewModel = nullptr;

    FlowLayout* m_folderTagsLayout = nullptr;
    TagSearchEdit* m_searchEdit = nullptr;
    FlowLayout* m_searchResultsLayout = nullptr;
    QPushButton* m_createTagButton = nullptr;
    FlowLayout* m_selectedItemTagsLayout = nullptr;
    QComboBox* m_selectedItemTagsCombineModeCombo = nullptr;

    struct ThemedSection
    {
        QWidget* content = nullptr;
        QWidget* viewport = nullptr;
        ColorRole colorRole = nullptr;
    };
    std::vector<ThemedSection> m_themedSections;
};
