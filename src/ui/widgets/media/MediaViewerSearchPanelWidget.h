#pragma once

#include <vector>

#include <QWidget>

#include "MediaViewerKind.h"
#include "MediaViewerTagBehaviour.h"
#include "NameSearchOptions.h"
#include "Tag.h"
#include "UiColors.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QToolButton;
class QLabel;
class FlowLayout;
class UiThemeChangeNotifier;

// Content of the right panel's "Searcher" tab (Architecture.md §14.61): Images/Videos kind picker,
// an "Exit viewer mode" button, a removable applied-tag-filter chip row, a tag-search box with its
// live matching-tag chip row, and the Match Case/Whole Word/Exact Name/Match Regex/Skip
// Git-Ignored toggle row plus a name field -- all for Media Viewer mode's full-pane gallery. One
// long-lived, window-wide instance (like SearchPanelWidget/TagPanelWidget), retargeted by
// MainWindow::onFocusedTabChanged to whichever tab is currently focused rather than holding a
// TabViewModel pointer of its own.
class MediaViewerSearchPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MediaViewerSearchPanelWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

    // QSignalBlocker-guarded resync, mirrors SearchCriteriaPanel::setCriteria's resync posture.
    void setKind(MediaViewerKind kind);
    void setAppliedTagFilters(const std::vector<Tag>& tags);
    void setTagSearchResults(const std::vector<Tag>& tags);
    void setNameFilter(const QString& query, const NameSearchOptions& options);
    void setLoading(bool loading);
    void setTagBehaviour(MediaViewerTagBehaviour behaviour);

signals:
    void kindChanged(MediaViewerKind kind);
    void exitRequested();
    void tagBehaviourChanged(MediaViewerTagBehaviour behaviour);
    // Relays a search-results tag chip's click as-is -- the Filter/Apply decision lives in
    // TabViewModel::handleMediaViewerSearchTagActivated, not here (this widget stays "dumb", same
    // posture as kindChanged/tagFilterRemoved).
    void tagSearchChipActivated(Tag::Id tagId, Qt::KeyboardModifiers modifiers);
    void tagFilterRemoved(Tag::Id tagId);
    void tagSearchQueryRequested(const QString& query);
    void nameFilterChanged(const QString& query, const NameSearchOptions& options);

private:
    // Pointer-to-member into UiColors::UiColorSet, mirrors TagPanelWidget::ColorRole -- resolved
    // against UiColors::current() both at construction and again on every themeChanged
    // (Architecture.md §14.50).
    using ColorRole = const char* UiColors::UiColorSet::*;

    NameSearchOptions currentNameOptions() const;
    void emitNameFilterChanged();
    static void clearLayout(FlowLayout* layout);

    // Ports SearchCriteriaPanel::onMatchModeButtonToggled's hand-rolled "exclusive but
    // deselectable" behavior for Whole Word/Exact Name/Regex.
    void onMatchModeButtonToggled(QToolButton* button, bool checked);

    // Mirrors TagPanelWidget::addChipSection: wraps a chip FlowLayout in its own QScrollArea so a
    // growing/shrinking chip list resizes within that scroll area instead of reflowing (and
    // visually "jumping") the rest of the panel below it. `title` is omitted when empty (the
    // tag-search section's field already labels itself via placeholder text). colorRole selects
    // which UiColorSet member tints the section (TagPanelWidget reuses the same two roles for its
    // analogous removable/matching chip sections); the section is recorded in m_themedSections so
    // applyThemeColors() can re-tint it later.
    FlowLayout* addChipSection(class QVBoxLayout* mainLayout, const QString& title, ColorRole colorRole,
                                const QString& sectionObjectName);

    // Re-applies every recorded chip section's tint from UiColors::current() (Architecture.md
    // §14.50).
    void applyThemeColors();

    QComboBox* m_kindCombo = nullptr;
    QToolButton* m_exitButton = nullptr;
    QLabel* m_loadingLabel = nullptr;

    FlowLayout* m_appliedFiltersLayout = nullptr;

    QLineEdit* m_tagSearchEdit = nullptr;
    QComboBox* m_tagBehaviourCombo = nullptr;
    FlowLayout* m_tagMatchesLayout = nullptr;

    QToolButton* m_matchCaseButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_exactNameButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_skipGitIgnoredButton = nullptr;
    QLineEdit* m_nameEdit = nullptr;

    struct ThemedSection
    {
        QWidget* content = nullptr;
        QWidget* viewport = nullptr;
        ColorRole colorRole = nullptr;
    };
    std::vector<ThemedSection> m_themedSections;
};
