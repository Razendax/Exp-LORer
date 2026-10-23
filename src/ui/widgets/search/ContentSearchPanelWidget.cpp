#include "ContentSearchPanelWidget.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "ContentSearchResultDelegate.h"
#include "ContentSearchResultsModel.h"
#include "ExclusiveToggleButtons.h"
#include "PathContainment.h"

namespace
{
    // Same debounce as SearchPanelWidget's quick-search box (Architecture.md §14.46) -- content
    // search is heavier per spec, but typing cadence is the same concern either way.
    constexpr int kSearchDebounceMs = 500;
}

ContentSearchPanelWidget::ContentSearchPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contentSearchPanelWidget"));

    auto* iconToolBar = new QWidget(this);
    auto* iconLayout = new QHBoxLayout(iconToolBar);
    iconLayout->setContentsMargins(2, 2, 2, 2);
    iconLayout->setSpacing(2);

    m_refreshButton = new QToolButton(iconToolBar);
    m_refreshButton->setObjectName(QStringLiteral("contentSearchRefreshButton"));
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh"));
    connect(m_refreshButton, &QToolButton::clicked, this, [this]() { emit refreshRequested(m_searchBar->text()); });
    iconLayout->addWidget(m_refreshButton);

    m_clearButton = new QToolButton(iconToolBar);
    m_clearButton->setObjectName(QStringLiteral("contentSearchClearButton"));
    m_clearButton->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
    m_clearButton->setToolTip(tr("Clear"));
    connect(m_clearButton, &QToolButton::clicked, this, [this]() {
        m_searchDebounceTimer->stop();
        m_searchBar->clear();
        emit queryChanged(QString());
        emit clearRequested();
    });
    iconLayout->addWidget(m_clearButton);

    m_expandAllButton = new QToolButton(iconToolBar);
    m_expandAllButton->setObjectName(QStringLiteral("contentSearchExpandAllButton"));
    m_expandAllButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_expandAllButton->setText(tr("Expand All"));
    connect(m_expandAllButton, &QToolButton::clicked, this, [this]() { m_resultsView->expandAll(); });
    iconLayout->addWidget(m_expandAllButton);

    m_collapseAllButton = new QToolButton(iconToolBar);
    m_collapseAllButton->setObjectName(QStringLiteral("contentSearchCollapseAllButton"));
    m_collapseAllButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_collapseAllButton->setText(tr("Collapse All"));
    connect(m_collapseAllButton, &QToolButton::clicked, this, [this]() { m_resultsView->collapseAll(); });
    iconLayout->addWidget(m_collapseAllButton);

    iconLayout->addStretch();

    auto* toggleRow = new QWidget(this);
    auto* toggleLayout = new QHBoxLayout(toggleRow);
    toggleLayout->setContentsMargins(2, 0, 2, 2);
    toggleLayout->setSpacing(2);

    m_matchCaseButton = new QToolButton(toggleRow);
    m_matchCaseButton->setObjectName(QStringLiteral("contentSearchMatchCaseButton"));
    m_matchCaseButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_matchCaseButton->setText(tr("Aa"));
    m_matchCaseButton->setCheckable(true);
    m_matchCaseButton->setToolTip(tr("Match Case\nE.g. \"Text\" will not match \"text\""));
    connect(m_matchCaseButton, &QToolButton::toggled, this, &ContentSearchPanelWidget::emitSearchOptionsChanged);
    toggleLayout->addWidget(m_matchCaseButton);

    m_wholeWordButton = new QToolButton(toggleRow);
    m_wholeWordButton->setObjectName(QStringLiteral("contentSearchWholeWordButton"));
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(
        tr("Match Whole Word\nMatches whole word only, not in between. Case insensitive unless Match Case is also on."));
    connect(m_wholeWordButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_wholeWordButton, m_regexButton, checked); });
    toggleLayout->addWidget(m_wholeWordButton);

    m_regexButton = new QToolButton(toggleRow);
    m_regexButton->setObjectName(QStringLiteral("contentSearchRegexButton"));
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));
    connect(m_regexButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_regexButton, m_wholeWordButton, checked); });
    toggleLayout->addWidget(m_regexButton);

    m_skipGitIgnoredButton = new QToolButton(toggleRow);
    m_skipGitIgnoredButton->setObjectName(QStringLiteral("contentSearchSkipGitIgnoredButton"));
    m_skipGitIgnoredButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_skipGitIgnoredButton->setText(tr(".gi"));
    m_skipGitIgnoredButton->setCheckable(true);
    m_skipGitIgnoredButton->setToolTip(
        tr("Skip Git-Ignored\nSkips files and folders excluded by .gitignore when searching inside a git working tree"));
    connect(m_skipGitIgnoredButton, &QToolButton::toggled, this, &ContentSearchPanelWidget::emitSearchOptionsChanged);
    toggleLayout->addWidget(m_skipGitIgnoredButton);

    toggleLayout->addStretch();

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(kSearchDebounceMs);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() { emit queryChanged(m_searchBar->text()); });

    m_searchBar = new QLineEdit(this);
    m_searchBar->setObjectName(QStringLiteral("contentSearchSearchBar"));
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setPlaceholderText(tr("Find in files..."));
    connect(m_searchBar, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_delegate->setHighlightQuery(text, currentQueryOptions());

        if (text.trimmed().isEmpty())
        {
            m_searchDebounceTimer->stop();
            emit queryChanged(text);
        }
        else
        {
            m_searchDebounceTimer->start();
        }
    });
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        m_searchDebounceTimer->stop();
        emit refreshRequested(m_searchBar->text());
    });

    // Collapsible "Search Details" disclosure (Include/Exclude fields), collapsed by default.
    m_detailsDisclosureButton = new QToolButton(this);
    m_detailsDisclosureButton->setObjectName(QStringLiteral("contentSearchDetailsDisclosureButton"));
    m_detailsDisclosureButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_detailsDisclosureButton->setText(tr("Search Details"));
    m_detailsDisclosureButton->setCheckable(true);
    m_detailsDisclosureButton->setAutoRaise(true);
    connect(m_detailsDisclosureButton, &QToolButton::toggled, this, &ContentSearchPanelWidget::setDetailsVisible);

    m_detailsContent = new QWidget(this);
    auto* detailsLayout = new QVBoxLayout(m_detailsContent);
    detailsLayout->setContentsMargins(4, 0, 4, 4);
    detailsLayout->setSpacing(4);

    // Shared by the Include/Exclude rows below -- each is a QLineEdit plus its own independent
    // Match Case/Whole Word/Regex toggle trio (same exclusive-group logic as the main query's
    // toggle row, distinct object names/instances since all three rows can be on screen at once).
    auto buildPatternRow = [this, detailsLayout](const QString& placeholder, const QString& editObjectName, QLineEdit*& editOut,
                                                  QToolButton*& caseOut, QToolButton*& wholeWordOut, QToolButton*& regexOut,
                                                  const QString& caseObjectName, const QString& wholeWordObjectName,
                                                  const QString& regexObjectName) {
        auto* row = new QWidget(m_detailsContent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(2);

        editOut = new QLineEdit(row);
        editOut->setObjectName(editObjectName);
        editOut->setClearButtonEnabled(true);
        editOut->setPlaceholderText(placeholder);
        connect(editOut, &QLineEdit::textEdited, this, [this](const QString&) { emitSearchOptionsChanged(); });
        rowLayout->addWidget(editOut, /*stretch=*/1);

        caseOut = new QToolButton(row);
        caseOut->setObjectName(caseObjectName);
        caseOut->setToolButtonStyle(Qt::ToolButtonTextOnly);
        caseOut->setText(tr("Aa"));
        caseOut->setCheckable(true);
        caseOut->setToolTip(tr("Match Case"));
        connect(caseOut, &QToolButton::toggled, this, &ContentSearchPanelWidget::emitSearchOptionsChanged);
        rowLayout->addWidget(caseOut);

        wholeWordOut = new QToolButton(row);
        wholeWordOut->setObjectName(wholeWordObjectName);
        wholeWordOut->setToolButtonStyle(Qt::ToolButtonTextOnly);
        wholeWordOut->setText(tr("ab"));
        wholeWordOut->setCheckable(true);
        wholeWordOut->setToolTip(tr("Match Whole Word"));
        rowLayout->addWidget(wholeWordOut);

        regexOut = new QToolButton(row);
        regexOut->setObjectName(regexObjectName);
        regexOut->setToolButtonStyle(Qt::ToolButtonTextOnly);
        regexOut->setText(tr(".*"));
        regexOut->setCheckable(true);
        regexOut->setToolTip(tr("Match Regex"));
        rowLayout->addWidget(regexOut);

        connect(wholeWordOut, &QToolButton::toggled, this,
                [this, wholeWordOut, regexOut](bool checked) { onMatchModeButtonToggled(wholeWordOut, regexOut, checked); });
        connect(regexOut, &QToolButton::toggled, this,
                [this, wholeWordOut, regexOut](bool checked) { onMatchModeButtonToggled(regexOut, wholeWordOut, checked); });

        detailsLayout->addWidget(row);
    };

    buildPatternRow(tr("Files to include (e.g. *.cpp)"), QStringLiteral("contentSearchIncludeEdit"), m_includeEdit,
                    m_includeMatchCaseButton, m_includeWholeWordButton, m_includeRegexButton,
                    QStringLiteral("contentSearchIncludeMatchCaseButton"), QStringLiteral("contentSearchIncludeWholeWordButton"),
                    QStringLiteral("contentSearchIncludeRegexButton"));

    buildPatternRow(tr("Files to exclude"), QStringLiteral("contentSearchExcludeEdit"), m_excludeEdit, m_excludeMatchCaseButton,
                    m_excludeWholeWordButton, m_excludeRegexButton, QStringLiteral("contentSearchExcludeMatchCaseButton"),
                    QStringLiteral("contentSearchExcludeWholeWordButton"), QStringLiteral("contentSearchExcludeRegexButton"));

    m_detailsContent->setVisible(false);

    m_headerCountLabel = new QLabel(this);
    m_headerCountLabel->setObjectName(QStringLiteral("contentSearchHeaderCountLabel"));
    m_headerCountLabel->setVisible(false);

    m_resultsModel = new ContentSearchResultsModel(this);
    m_resultsView = new QTreeView(this);
    m_resultsView->setObjectName(QStringLiteral("contentSearchResultsView"));
    m_resultsView->setModel(m_resultsModel);
    m_resultsView->setHeaderHidden(true);
    m_resultsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsView->setUniformRowHeights(true);
    m_delegate = new ContentSearchResultDelegate(m_resultsView);
    m_resultsView->setItemDelegate(m_delegate);
    connect(m_resultsView, &QTreeView::doubleClicked, this, &ContentSearchPanelWidget::onTreeDoubleClicked);
    connect(m_resultsView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { onTreeSelectionChanged(); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(iconToolBar);
    layout->addWidget(m_searchBar);
    layout->addWidget(toggleRow);
    layout->addWidget(m_detailsDisclosureButton);
    layout->addWidget(m_detailsContent);
    layout->addWidget(m_headerCountLabel);
    layout->addWidget(m_resultsView, /*stretch=*/1);

    setDetailsVisible(false);
    clearForNoTab();
}

TextMatchOptions ContentSearchPanelWidget::currentQueryOptions() const
{
    TextMatchOptions options;
    options.caseSensitive = m_matchCaseButton->isChecked();
    if (m_wholeWordButton->isChecked())
    {
        options.mode = TextMatchMode::WholeWord;
    }
    else if (m_regexButton->isChecked())
    {
        options.mode = TextMatchMode::Regex;
    }
    return options;
}

void ContentSearchPanelWidget::setQuery(const QString& query)
{
    setEnabled(true);

    m_searchDebounceTimer->stop();

    const QSignalBlocker blocker(m_searchBar);
    m_searchBar->setText(query);

    m_delegate->setHighlightQuery(query, currentQueryOptions());
}

void ContentSearchPanelWidget::setOptions(const ContentSearchOptions& options)
{
    const QSignalBlocker b1(m_matchCaseButton);
    const QSignalBlocker b2(m_wholeWordButton);
    const QSignalBlocker b3(m_regexButton);
    const QSignalBlocker b4(m_skipGitIgnoredButton);
    const QSignalBlocker b5(m_includeEdit);
    const QSignalBlocker b6(m_includeMatchCaseButton);
    const QSignalBlocker b7(m_includeWholeWordButton);
    const QSignalBlocker b8(m_includeRegexButton);
    const QSignalBlocker b9(m_excludeEdit);
    const QSignalBlocker b10(m_excludeMatchCaseButton);
    const QSignalBlocker b11(m_excludeWholeWordButton);
    const QSignalBlocker b12(m_excludeRegexButton);

    m_matchCaseButton->setChecked(options.queryOptions.caseSensitive);
    m_wholeWordButton->setChecked(options.queryOptions.mode == TextMatchMode::WholeWord);
    m_regexButton->setChecked(options.queryOptions.mode == TextMatchMode::Regex);
    m_skipGitIgnoredButton->setChecked(options.skipGitIgnored);

    m_includeEdit->setText(QString::fromStdString(options.includePattern));
    m_includeMatchCaseButton->setChecked(options.includeOptions.caseSensitive);
    m_includeWholeWordButton->setChecked(options.includeOptions.mode == TextMatchMode::WholeWord);
    m_includeRegexButton->setChecked(options.includeOptions.mode == TextMatchMode::Regex);

    m_excludeEdit->setText(QString::fromStdString(options.excludePattern));
    m_excludeMatchCaseButton->setChecked(options.excludeOptions.caseSensitive);
    m_excludeWholeWordButton->setChecked(options.excludeOptions.mode == TextMatchMode::WholeWord);
    m_excludeRegexButton->setChecked(options.excludeOptions.mode == TextMatchMode::Regex);

    m_delegate->setHighlightQuery(m_searchBar->text(), options.queryOptions);
}

void ContentSearchPanelWidget::onMatchModeButtonToggled(QToolButton* button, QToolButton* otherButton, bool checked)
{
    applyExclusiveToggle(button, checked, { otherButton });
    emitSearchOptionsChanged();
}

void ContentSearchPanelWidget::emitSearchOptionsChanged()
{
    ContentSearchOptions options;
    options.queryOptions = currentQueryOptions();
    options.skipGitIgnored = m_skipGitIgnoredButton->isChecked();

    options.includePattern = m_includeEdit->text().toStdString();
    options.includeOptions.caseSensitive = m_includeMatchCaseButton->isChecked();
    if (m_includeWholeWordButton->isChecked())
    {
        options.includeOptions.mode = TextMatchMode::WholeWord;
    }
    else if (m_includeRegexButton->isChecked())
    {
        options.includeOptions.mode = TextMatchMode::Regex;
    }

    options.excludePattern = m_excludeEdit->text().toStdString();
    options.excludeOptions.caseSensitive = m_excludeMatchCaseButton->isChecked();
    if (m_excludeWholeWordButton->isChecked())
    {
        options.excludeOptions.mode = TextMatchMode::WholeWord;
    }
    else if (m_excludeRegexButton->isChecked())
    {
        options.excludeOptions.mode = TextMatchMode::Regex;
    }

    m_delegate->setHighlightQuery(m_searchBar->text(), options.queryOptions);

    emit searchOptionsChanged(options);
}

void ContentSearchPanelWidget::setDetailsVisible(bool visible)
{
    m_detailsContent->setVisible(visible);
    m_detailsDisclosureButton->setIcon(style()->standardIcon(visible ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
}

void ContentSearchPanelWidget::setResults(const std::vector<FileContentMatch>& matches, const std::filesystem::path& root)
{
    m_lastResults = matches;

    const std::vector<std::filesystem::path> collapsedPaths = collectCollapsedPaths();

    m_resultsModel->setResults(matches, root);
    m_resultsView->expandAll();
    restoreCollapsedPaths(collapsedPaths);

    updateHeaderCountLabel();
}

std::vector<std::filesystem::path> ContentSearchPanelWidget::collectCollapsedPaths() const
{
    std::vector<std::filesystem::path> collapsed;

    const int rows = m_resultsModel->rowCount(QModelIndex());
    for (int i = 0; i < rows; ++i)
    {
        const QModelIndex index = m_resultsModel->index(i, 0, QModelIndex());
        if (!m_resultsView->isExpanded(index))
        {
            collapsed.emplace_back(index.data(ContentSearchResultsModel::PathRole).toString().toStdWString());
        }
    }

    return collapsed;
}

void ContentSearchPanelWidget::restoreCollapsedPaths(const std::vector<std::filesystem::path>& collapsedPaths)
{
    if (collapsedPaths.empty())
    {
        return;
    }

    const int rows = m_resultsModel->rowCount(QModelIndex());
    for (int i = 0; i < rows; ++i)
    {
        const QModelIndex index = m_resultsModel->index(i, 0, QModelIndex());
        const std::filesystem::path path(index.data(ContentSearchResultsModel::PathRole).toString().toStdWString());
        const bool wasCollapsed = std::any_of(collapsedPaths.begin(), collapsedPaths.end(),
                                               [&path](const std::filesystem::path& p) { return pathsEqualCaseInsensitive(p, path); });
        if (wasCollapsed)
        {
            m_resultsView->setExpanded(index, false);
        }
    }
}

void ContentSearchPanelWidget::updateHeaderCountLabel()
{
    const int fileCount = m_resultsModel->fileCount();
    if (fileCount == 0)
    {
        m_headerCountLabel->setVisible(false);
        return;
    }

    m_headerCountLabel->setText(tr("%1 matches in %2 files").arg(m_resultsModel->matchCount()).arg(fileCount));
    m_headerCountLabel->setVisible(true);
}

void ContentSearchPanelWidget::onTreeDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
    {
        return;
    }

    const QVariant lineNumber = index.data(ContentSearchResultsModel::LineNumberRole);
    const std::filesystem::path path(index.data(ContentSearchResultsModel::PathRole).toString().toStdWString());

    if (lineNumber.isValid())
    {
        emit lineActivated(path, lineNumber.toInt());
    }
    else
    {
        emit resultActivated(path, /*isDirectory=*/false);
    }
}

void ContentSearchPanelWidget::onTreeSelectionChanged()
{
    const QModelIndexList selected = m_resultsView->selectionModel()->selectedRows();
    if (selected.isEmpty())
    {
        emit selectionChanged({});
        return;
    }

    const std::filesystem::path path(selected.first().data(ContentSearchResultsModel::PathRole).toString().toStdWString());
    const auto it = std::find_if(m_lastResults.begin(), m_lastResults.end(),
                                  [&path](const FileContentMatch& match) { return pathsEqualCaseInsensitive(match.node.path(), path); });
    if (it == m_lastResults.end())
    {
        emit selectionChanged({});
        return;
    }

    emit selectionChanged({ it->node });
}

void ContentSearchPanelWidget::setLoading(bool loading)
{
    m_refreshButton->setEnabled(!loading);
}

void ContentSearchPanelWidget::clearForNoTab()
{
    const QSignalBlocker blocker(m_searchBar);
    m_searchBar->clear();
    m_lastResults.clear();
    m_resultsModel->setResults({}, {});
    setOptions({});
    m_headerCountLabel->setVisible(false);
    setEnabled(false);
}
