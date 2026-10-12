#pragma once

#include <vector>

#include <QWidget>

#include "NameSearchOptions.h"
#include "SearchCriteria.h"
#include "Tag.h"

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QToolButton;
class QTimer;
class FlowLayout;
class UiThemeChangeNotifier;

// Name / Min size / Max size fields plus a Search button, a tag-search box with its
// live matching-tag chip row, and a removable tag-criteria chip row, for the advanced (criteria)
// search pane (Architecture.md §14.19/§14.22). Lives at the top of SearchResultsPane, one
// instance per tab (inside WorkspacePaneWidget's per-tab stack page).
class SearchCriteriaPanel : public QWidget
{
    Q_OBJECT

public:
    SearchCriteriaPanel(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

    // Repopulates the fields without emitting searchRequested/criteriaEdited, e.g. when a tab
    // regains focus (mirrors MainWindow::onFocusedTabChanged's resync of the quick-search box).
    // criteriaTags are the resolved Tag objects for criteria.tagIds (for chip display).
    void setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags);

    // Rebuilds the live matching-tag chip row from a tag search lookup.
    void setTagSearchResults(const std::vector<Tag>& tags);

    // Swaps the Search button's text to "Searching…" while true, restoring it otherwise. The
    // button stays enabled throughout -- clicking it (or editing a field) while a search is still
    // running restarts it with the latest criteria. Called around TabViewModel::advancedSearchLoading
    // and the advancedSearchLoadingFinished/advancedSearchFailed signals that follow it
    // (Architecture.md §14.37).
    void setSearching(bool searching);

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
    // Emits criteriaEdited immediately. Called directly by the toggle buttons (discrete clicks,
    // like SearchPanelWidget's options row) and by m_criteriaDebounceTimer's timeout for Name/size
    // field edits (continuous typing/spinning, debounced -- see scheduleCriteriaEdited).
    void onAnyFieldEdited();
    // Name/size field edits funnel through here instead of onAnyFieldEdited directly: restarts
    // m_criteriaDebounceTimer (Architecture.md §14.56) so a live re-scan isn't kicked off on every
    // keystroke/arrow-click, except an empty Name field, which bypasses the debounce so clearing it
    // reverts to the plain folder listing immediately (matches the Search button/Enter path).
    void scheduleCriteriaEdited(bool bypassDebounceIfTextEmpty = false);
    static void clearLayout(FlowLayout* layout);

    // Re-applies m_closeButton's UiColors::current()-based stylesheet (Architecture.md §14.50).
    void applyThemeColors();

    // Ports SearchPanelWidget::onMatchModeButtonToggled's hand-rolled "exclusive but deselectable"
    // behavior for Whole Word/Exact Name/Regex (Architecture.md §14.56) -- same vendored-Qt
    // QButtonGroup limitation (no ExclusionPolicy::ExclusiveOptional).
    void onMatchModeButtonToggled(QToolButton* button, bool checked);

    QLineEdit* m_nameEdit = nullptr;
    // Plain magnitude + unit combo (KB/MB/GB) pair, replacing a single fixed-KB spin box
    // (Architecture.md §14.56) -- currentCriteria()/setCriteria() convert via the combo's
    // selection. The combo is sticky UI-only state (not carried by SearchCriteria itself): setCriteria
    // never changes it, only recomputes the spin box's number against whatever unit is selected.
    QSpinBox* m_minSizeSpin = nullptr;
    QComboBox* m_minSizeUnitCombo = nullptr;
    QSpinBox* m_maxSizeSpin = nullptr;
    QComboBox* m_maxSizeUnitCombo = nullptr;
    QPushButton* m_searchButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    // Debounces Name/size field edits (Architecture.md §14.56) -- see scheduleCriteriaEdited.
    QTimer* m_criteriaDebounceTimer = nullptr;

    // Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles (Architecture.md
    // §14.56) -- same NameSearchOptions-backed behavior as SearchPanelWidget's equivalent 5
    // buttons, but distinct object names/instances since both can be on screen at once.
    QToolButton* m_matchCaseButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_exactNameButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_skipGitIgnoredButton = nullptr;

    QLineEdit* m_tagSearchEdit = nullptr;
    // Exact/Include-Children toggle for the tag criteria below (Architecture.md §14.87) -- when
    // checked, a selected tag also matches its "/"-nested children (e.g. "game" matches
    // "game/action"). Grouped with the tag controls rather than matchOptionsRow's name-matching
    // toggles, since it's tag-specific.
    QToolButton* m_tagIncludeChildrenButton = nullptr;
    FlowLayout* m_tagMatchesLayout = nullptr;
    FlowLayout* m_tagCriteriaLayout = nullptr;

    // tagIds from the last setCriteria() call — currentCriteria() carries these through so an
    // edit to the Name/size fields (or pressing Search) doesn't clobber tag criteria that were
    // added via chip clicks rather than through this panel's own fields.
    std::vector<Tag::Id> m_lastKnownTagIds;
};
