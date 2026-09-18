#include "SearchResultsPane.h"

#include <QVBoxLayout>

#include "FileBrowserView.h"
#include "SearchCriteriaPanel.h"

SearchResultsPane::SearchResultsPane(FileListModel* resultsModel, QWidget* parent)
    : QWidget(parent)
{
    m_criteriaPanel = new SearchCriteriaPanel(this);
    m_browserView = new FileBrowserView(resultsModel, this, FileBrowserView::DisplayMode::AdvancedSearchResults);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_criteriaPanel);
    layout->addWidget(m_browserView);
}

void SearchResultsPane::setHighlightQuery(const QString& query)
{
    m_browserView->setNameHighlightQuery(query);
}

void SearchResultsPane::setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags)
{
    m_criteriaPanel->setCriteria(criteria, criteriaTags);
    setHighlightQuery(QString::fromStdString(criteria.nameQuery));
}
