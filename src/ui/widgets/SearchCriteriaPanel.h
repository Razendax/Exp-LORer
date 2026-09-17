#pragma once

#include <QWidget>

#include "SearchCriteria.h"

class QLineEdit;
class QSpinBox;
class QPushButton;
class QToolButton;

// Name / Min size / Max size / Extension fields plus a Search button for the advanced (criteria)
// search pane (Architecture.md §14.19). Lives at the top of SearchResultsPane, one instance per
// tab (inside WorkspacePaneWidget's per-tab stack page).
class SearchCriteriaPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchCriteriaPanel(QWidget* parent = nullptr);

    // Repopulates the fields without emitting searchRequested/criteriaEdited, e.g. when a tab
    // regains focus (mirrors MainWindow::onFocusedTabChanged's resync of the quick-search box).
    void setCriteria(const SearchCriteria& criteria);

signals:
    // Search button clicked, or Enter pressed in the Name field (matches the Enter-triggers-scan
    // convention already used by the quick search box and address bar).
    void searchRequested(const SearchCriteria& criteria);

    // Any field edited live. Only meaningful once a search is already active — the consumer
    // (TabViewModel::updateAdvancedSearchCriteria) no-ops otherwise.
    void criteriaEdited(const SearchCriteria& criteria);

    void closeRequested();

private:
    SearchCriteria currentCriteria() const;
    void onAnyFieldEdited();

    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_minSizeKbSpin = nullptr;
    QSpinBox* m_maxSizeKbSpin = nullptr;
    QLineEdit* m_extensionEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QToolButton* m_closeButton = nullptr;
};
