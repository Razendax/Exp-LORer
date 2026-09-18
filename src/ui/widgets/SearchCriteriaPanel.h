#pragma once

#include <vector>

#include <QWidget>

#include "SearchCriteria.h"
#include "Tag.h"

class QLineEdit;
class QSpinBox;
class QPushButton;
class QToolButton;
class FlowLayout;

// Name / Min size / Max size / Extension fields plus a Search button, a tag-search box with its
// live matching-tag chip row, and a removable tag-criteria chip row, for the advanced (criteria)
// search pane (Architecture.md §14.19/§14.22). Lives at the top of SearchResultsPane, one
// instance per tab (inside WorkspacePaneWidget's per-tab stack page).
class SearchCriteriaPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchCriteriaPanel(QWidget* parent = nullptr);

    // Repopulates the fields without emitting searchRequested/criteriaEdited, e.g. when a tab
    // regains focus (mirrors MainWindow::onFocusedTabChanged's resync of the quick-search box).
    // criteriaTags are the resolved Tag objects for criteria.tagIds (for chip display).
    void setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags);

    // Rebuilds the live matching-tag chip row from a tag search lookup.
    void setTagSearchResults(const std::vector<Tag>& tags);

signals:
    // Search button clicked, or Enter pressed in the Name field (matches the Enter-triggers-scan
    // convention already used by the quick search box and address bar).
    void searchRequested(const SearchCriteria& criteria);

    // Any field edited live. Only meaningful once a search is already active — the consumer
    // (TabViewModel::updateAdvancedSearchCriteria) no-ops otherwise.
    void criteriaEdited(const SearchCriteria& criteria);

    void closeRequested();

    // m_tagSearchEdit textChanged.
    void tagSearchQueryRequested(const QString& query);
    // A matches-row chip was clicked.
    void tagCriterionAdded(Tag::Id tagId);
    // A criteria-row chip's "x" was clicked.
    void tagCriterionRemoved(Tag::Id tagId);

private:
    SearchCriteria currentCriteria() const;
    void onAnyFieldEdited();
    static void clearLayout(FlowLayout* layout);

    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_minSizeKbSpin = nullptr;
    QSpinBox* m_maxSizeKbSpin = nullptr;
    QLineEdit* m_extensionEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QToolButton* m_closeButton = nullptr;

    QLineEdit* m_tagSearchEdit = nullptr;
    FlowLayout* m_tagMatchesLayout = nullptr;
    FlowLayout* m_tagCriteriaLayout = nullptr;

    // tagIds from the last setCriteria() call — currentCriteria() carries these through so an
    // edit to the Name/size/extension fields (or pressing Search) doesn't clobber tag criteria
    // that were added via chip clicks rather than through this panel's own fields.
    std::vector<Tag::Id> m_lastKnownTagIds;
};
