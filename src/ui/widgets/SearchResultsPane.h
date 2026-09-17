#pragma once

#include <QString>
#include <QWidget>

#include "SearchCriteria.h"

class FileListModel;
class FileBrowserView;
class SearchCriteriaPanel;

// SearchCriteriaPanel (top) + a FileBrowserView constructed in AdvancedSearchResults mode
// (bottom), for the advanced (criteria) search pane (Architecture.md §14.19). One instance per
// tab, held as the third page of WorkspacePaneWidget's per-tab QStackedWidget.
class SearchResultsPane : public QWidget
{
    Q_OBJECT

public:
    explicit SearchResultsPane(FileListModel* resultsModel, QWidget* parent = nullptr);

    FileBrowserView* browserView() const noexcept { return m_browserView; }
    SearchCriteriaPanel* criteriaPanel() const noexcept { return m_criteriaPanel; }

    void setHighlightQuery(const QString& query);
    void setCriteria(const SearchCriteria& criteria);

private:
    SearchCriteriaPanel* m_criteriaPanel = nullptr;
    FileBrowserView* m_browserView = nullptr;
};
