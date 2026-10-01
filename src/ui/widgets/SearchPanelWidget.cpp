#include "SearchPanelWidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>

#include "FileBrowserView.h"
#include "FileListModel.h"
#include "PathContainment.h"
#include "SearchResultDelegate.h"
#include "SearchResultsTreeModel.h"
#include "UiColors.h"

SearchPanelWidget::SearchPanelWidget(const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                                      QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("searchPanelWidget"));

    auto* iconToolBar = new QWidget(this);
    auto* iconLayout = new QHBoxLayout(iconToolBar);
    iconLayout->setContentsMargins(2, 2, 2, 2);
    iconLayout->setSpacing(2);

    m_refreshButton = new QToolButton(iconToolBar);
    m_refreshButton->setObjectName(QStringLiteral("searchRefreshButton"));
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh"));
    connect(m_refreshButton, &QToolButton::clicked, this, [this]() { emit refreshRequested(m_searchBar->text()); });
    iconLayout->addWidget(m_refreshButton);

    m_clearButton = new QToolButton(iconToolBar);
    m_clearButton->setObjectName(QStringLiteral("searchClearButton"));
    m_clearButton->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
    m_clearButton->setToolTip(tr("Clear"));
    connect(m_clearButton, &QToolButton::clicked, this, [this]() {
        m_searchBar->clear();
        emit queryChanged(QString());
        emit clearRequested();
    });
    iconLayout->addWidget(m_clearButton);

    m_viewAsTreeButton = new QToolButton(iconToolBar);
    m_viewAsTreeButton->setObjectName(QStringLiteral("searchViewAsTreeButton"));
    m_viewAsTreeButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_viewAsTreeButton->setToolTip(tr("View as tree"));
    m_viewAsTreeButton->setCheckable(true);
    connect(m_viewAsTreeButton, &QToolButton::toggled, this, &SearchPanelWidget::setViewAsTree);
    iconLayout->addWidget(m_viewAsTreeButton);

    m_collapseAllButton = new QToolButton(iconToolBar);
    m_collapseAllButton->setObjectName(QStringLiteral("searchCollapseAllButton"));
    m_collapseAllButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_collapseAllButton->setText(tr("Collapse All"));
    m_collapseAllButton->setEnabled(false);
    connect(m_collapseAllButton, &QToolButton::clicked, this, [this]() { m_treeResultsView->collapseAll(); });
    iconLayout->addWidget(m_collapseAllButton);

    auto* matchOptionsDivider = new QFrame(iconToolBar);
    matchOptionsDivider->setFrameShape(QFrame::VLine);
    matchOptionsDivider->setFrameShadow(QFrame::Sunken);
    iconLayout->addWidget(matchOptionsDivider);

    // Match Case/Whole Word/Exact Name/Match Regex toggles (Architecture.md §14.44). Text-only,
    // same posture as m_collapseAllButton -- the project has no custom icon assets.
    m_matchCaseButton = new QToolButton(iconToolBar);
    m_matchCaseButton->setObjectName(QStringLiteral("searchMatchCaseButton"));
    m_matchCaseButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_matchCaseButton->setText(tr("Aa"));
    m_matchCaseButton->setCheckable(true);
    m_matchCaseButton->setToolTip(tr("Match Case\nE.g. \"File\" will not match \"file\""));
    connect(m_matchCaseButton, &QToolButton::toggled, this, &SearchPanelWidget::emitSearchOptionsChanged);
    iconLayout->addWidget(m_matchCaseButton);

    m_wholeWordButton = new QToolButton(iconToolBar);
    m_wholeWordButton->setObjectName(QStringLiteral("searchWholeWordButton"));
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(
        tr("Match Whole Word\nMatches whole word only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\", but will match \"File Name\""));
    connect(m_wholeWordButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_wholeWordButton, checked); });
    iconLayout->addWidget(m_wholeWordButton);

    m_exactNameButton = new QToolButton(iconToolBar);
    m_exactNameButton->setObjectName(QStringLiteral("searchExactNameButton"));
    m_exactNameButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_exactNameButton->setText(tr("="));
    m_exactNameButton->setCheckable(true);
    m_exactNameButton->setToolTip(
        tr("Exact Name\nMatches exact name only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\" nor \"File Name\", but will match only \"file\" or \"File\""));
    connect(m_exactNameButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_exactNameButton, checked); });
    iconLayout->addWidget(m_exactNameButton);

    m_regexButton = new QToolButton(iconToolBar);
    m_regexButton->setObjectName(QStringLiteral("searchRegexButton"));
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));
    connect(m_regexButton, &QToolButton::toggled, this, [this](bool checked) { onMatchModeButtonToggled(m_regexButton, checked); });
    iconLayout->addWidget(m_regexButton);

    iconLayout->addStretch();

    m_searchBar = new QLineEdit(this);
    m_searchBar->setObjectName(QStringLiteral("searchPanelSearchBar"));
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setPlaceholderText(tr("Search..."));
    connect(m_searchBar, &QLineEdit::textEdited, this, [this](const QString& text) {
        const bool caseSensitive = m_matchCaseButton->isChecked();
        m_listResultsView->setNameHighlightQuery(text, caseSensitive);
        m_treeDelegate->setHighlightQuery(text, caseSensitive);
        emit queryChanged(text);
    });
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() { emit refreshRequested(m_searchBar->text()); });

    m_advancedSearchButton = new QPushButton(tr("Advanced Search..."), this);
    m_advancedSearchButton->setObjectName(QStringLiteral("advancedSearchButton"));
    m_advancedSearchButton->setStyleSheet(QStringLiteral(
        "QPushButton#advancedSearchButton { background: %1; border: 1px solid %2; border-radius: 3px; }"
        "QPushButton#advancedSearchButton:hover { background: %3; }")
        .arg(QLatin1String(UiColors::kAdvancedSearchButtonBackground), QLatin1String(UiColors::kAccentBlue),
             QLatin1String(UiColors::kAdvancedSearchButtonHoverBackground)));
    connect(m_advancedSearchButton, &QPushButton::clicked, this, &SearchPanelWidget::advancedSearchRequested);

    m_listModel = new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this);

    m_resultsStack = new QStackedWidget(this);

    m_listResultsView = new FileBrowserView(m_listModel, m_resultsStack, FileBrowserView::DisplayMode::AdvancedSearchResults);
    m_listResultsView->setObjectName(QStringLiteral("searchListResultsView"));
    for (int column = FileListModel::NameColumn + 1; column < FileListModel::ColumnCount; ++column)
    {
        m_listResultsView->setColumnVisible(static_cast<FileListModel::Column>(column), false);
    }
    connect(m_listResultsView, &FileBrowserView::itemActivated, this, &SearchPanelWidget::resultActivated);
    connect(m_listResultsView, &FileBrowserView::selectionChanged, this, &SearchPanelWidget::selectionChanged);
    m_resultsStack->addWidget(m_listResultsView); // page 0: list

    m_treeModel = new SearchResultsTreeModel(this);
    m_treeResultsView = new QTreeView(m_resultsStack);
    m_treeResultsView->setObjectName(QStringLiteral("searchTreeResultsView"));
    m_treeResultsView->setModel(m_treeModel);
    m_treeResultsView->setHeaderHidden(true);
    m_treeResultsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeResultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeResultsView->setUniformRowHeights(true);
    m_treeDelegate = new SearchResultDelegate(m_treeResultsView);
    m_treeResultsView->setItemDelegate(m_treeDelegate);
    connect(m_treeResultsView, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
        {
            emit resultActivated(m_treeModel->pathForIndex(index), m_treeModel->isDirectoryForIndex(index));
        }
    });
    connect(m_treeResultsView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { onTreeSelectionChanged(); });
    m_resultsStack->addWidget(m_treeResultsView); // page 1: tree

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(iconToolBar);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_advancedSearchButton);
    layout->addWidget(m_resultsStack);

    clearForNoTab();
}

void SearchPanelWidget::setViewAsTree(bool tree)
{
    m_resultsStack->setCurrentIndex(tree ? 1 : 0);
    m_collapseAllButton->setEnabled(tree);
}

void SearchPanelWidget::setQuery(const QString& query)
{
    setEnabled(true);

    const QSignalBlocker blocker(m_searchBar);
    m_searchBar->setText(query);

    const bool caseSensitive = m_matchCaseButton->isChecked();
    m_listResultsView->setNameHighlightQuery(query, caseSensitive);
    m_treeDelegate->setHighlightQuery(query, caseSensitive);
}

void SearchPanelWidget::setOptions(const NameSearchOptions& options)
{
    const QSignalBlocker caseBlocker(m_matchCaseButton);
    const QSignalBlocker wholeWordBlocker(m_wholeWordButton);
    const QSignalBlocker exactNameBlocker(m_exactNameButton);
    const QSignalBlocker regexBlocker(m_regexButton);

    m_matchCaseButton->setChecked(options.caseSensitive);
    m_wholeWordButton->setChecked(options.mode == NameMatchMode::WholeWord);
    m_exactNameButton->setChecked(options.mode == NameMatchMode::ExactName);
    m_regexButton->setChecked(options.mode == NameMatchMode::Regex);

    m_listResultsView->setNameHighlightQuery(m_searchBar->text(), options.caseSensitive);
    m_treeDelegate->setHighlightQuery(m_searchBar->text(), options.caseSensitive);
}

void SearchPanelWidget::onMatchModeButtonToggled(QToolButton* button, bool checked)
{
    if (checked)
    {
        // Manual "exclusive but deselectable" behavior: this vendored Qt build's QButtonGroup has
        // no ExclusionPolicy::ExclusiveOptional (only the classic always-one-selected exclusive
        // mode), so the 3 buttons aren't in a QButtonGroup at all -- enforce "at most one of the 3
        // checked" here instead, while still letting a button be unchecked by clicking it again.
        for (QToolButton* other : { m_wholeWordButton, m_exactNameButton, m_regexButton })
        {
            if (other != button && other->isChecked())
            {
                const QSignalBlocker blocker(other);
                other->setChecked(false);
            }
        }
    }

    emitSearchOptionsChanged();
}

void SearchPanelWidget::emitSearchOptionsChanged()
{
    NameSearchOptions options;
    options.caseSensitive = m_matchCaseButton->isChecked();

    if (m_wholeWordButton->isChecked())
    {
        options.mode = NameMatchMode::WholeWord;
    }
    else if (m_exactNameButton->isChecked())
    {
        options.mode = NameMatchMode::ExactName;
    }
    else if (m_regexButton->isChecked())
    {
        options.mode = NameMatchMode::Regex;
    }

    const bool caseSensitive = options.caseSensitive;
    m_listResultsView->setNameHighlightQuery(m_searchBar->text(), caseSensitive);
    m_treeDelegate->setHighlightQuery(m_searchBar->text(), caseSensitive);

    emit searchOptionsChanged(options);
}

void SearchPanelWidget::setResults(const std::vector<FileNode>& entries, const std::filesystem::path& root)
{
    m_lastResults = entries;
    m_listModel->setEntries(root, entries);
    m_treeModel->setRoot(root);
    m_treeModel->setResults(entries);
}

void SearchPanelWidget::onTreeSelectionChanged()
{
    const QModelIndexList selected = m_treeResultsView->selectionModel()->selectedRows();
    if (selected.isEmpty())
    {
        emit selectionChanged({});
        return;
    }

    const std::filesystem::path path = m_treeModel->pathForIndex(selected.first());
    const auto it = std::find_if(m_lastResults.begin(), m_lastResults.end(),
                                  [&path](const FileNode& node) { return pathsEqualCaseInsensitive(node.path(), path); });
    if (it == m_lastResults.end())
    {
        // A structural ancestor node, not one of the matched entries -- treat as no selection
        // rather than adding a stat() fallback for this edge case (see SearchPanelWidget.h).
        emit selectionChanged({});
        return;
    }

    emit selectionChanged({*it});
}

void SearchPanelWidget::setLoading(bool loading)
{
    m_refreshButton->setEnabled(!loading);
}

void SearchPanelWidget::clearForNoTab()
{
    const QSignalBlocker blocker(m_searchBar);
    m_searchBar->clear();
    m_lastResults.clear();
    m_listModel->setEntries({}, {});
    m_treeModel->setRoot({});
    m_treeModel->setResults({});
    setOptions({});
    setEnabled(false);
}
