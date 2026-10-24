#include "SearchResultsPane.h"

#include <QVBoxLayout>

#include "FileBrowserView.h"
#include "SearchCriteriaPanel.h"

SearchResultsPane::SearchResultsPane(FileListModel* resultsModel, UiThemeChangeNotifier& uiThemeChangeNotifier,
                                      HotkeyManager& hotkeyManager, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("advancedSearchResultsPane"));

    m_criteriaPanel = new SearchCriteriaPanel(uiThemeChangeNotifier, this);
    m_browserView = new FileBrowserView(resultsModel, this, hotkeyManager, FileBrowserView::DisplayMode::AdvancedSearchResults);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_criteriaPanel);
    layout->addWidget(m_browserView);
}

void SearchResultsPane::setHighlightQuery(const QString& query, bool caseSensitive)
{
    m_browserView->setNameHighlightQuery(query, caseSensitive);
}

void SearchResultsPane::setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags)
{
    m_criteriaPanel->setCriteria(criteria, criteriaTags);
    setHighlightQuery(QString::fromStdString(criteria.nameQuery), criteria.nameOptions.caseSensitive);
}
