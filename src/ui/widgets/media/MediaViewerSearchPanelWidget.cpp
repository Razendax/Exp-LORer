#include "MediaViewerSearchPanelWidget.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "ExclusiveToggleButtons.h"
#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "TagSearchEdit.h"
#include "UiThemeChangeNotifier.h"

MediaViewerSearchPanelWidget::MediaViewerSearchPanelWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaViewerSearchPanelWidget"));

    m_kindCombo = new QComboBox(this);
    m_kindCombo->setObjectName(QStringLiteral("mediaViewerKindCombo"));
    m_kindCombo->addItem(tr("Images"));
    m_kindCombo->addItem(tr("Videos"));
    m_kindCombo->addItem(tr("Animations"));

    m_exitButton = new QToolButton(this);
    m_exitButton->setObjectName(QStringLiteral("mediaViewerExitButton"));
    m_exitButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_exitButton->setText(tr("Exit viewer mode"));

    m_loadingLabel = new QLabel(tr("Loading…"), this);
    m_loadingLabel->setObjectName(QStringLiteral("mediaViewerLoadingLabel"));
    m_loadingLabel->setVisible(false);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(m_kindCombo);
    topRow->addWidget(m_loadingLabel);
    topRow->addStretch();
    topRow->addWidget(m_exitButton);

    m_tagSearchEdit = new TagSearchEdit(this);
    m_tagSearchEdit->setObjectName(QStringLiteral("mediaViewerTagSearchEdit"));
    m_tagSearchEdit->setPlaceholderText(tr("Search tags..."));

    m_tagBehaviourCombo = new QComboBox(this);
    m_tagBehaviourCombo->setObjectName(QStringLiteral("mediaViewerTagBehaviourCombo"));
    m_tagBehaviourCombo->addItem(tr("Filter"));
    m_tagBehaviourCombo->addItem(tr("Apply"));

    auto* tagSearchRow = new QHBoxLayout();
    tagSearchRow->addWidget(m_tagSearchEdit);
    tagSearchRow->addWidget(m_tagBehaviourCombo);

    m_createTagButton = new QPushButton(this);
    m_createTagButton->setObjectName(QStringLiteral("mediaViewerCreateTagButton"));
    m_createTagButton->hide();

    // Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles, copied verbatim from
    // SearchCriteriaPanel's own row (Architecture.md §14.61) rather than extracted into a shared
    // widget -- this codebase already duplicates this row between SearchPanelWidget and
    // SearchCriteriaPanel instead of sharing it. Skip Git-Ignored is deliberately inert here: it
    // only ever mattered to decide whether to re-scan (TabQuickSearchViewModel::setOptions), and Media
    // Viewer's name/options edits never re-scan (TabViewModel::setMediaViewerNameFilter) --
    // FileNavigationUseCase::filterByName itself never reads the flag either way.
    m_matchCaseButton = new QToolButton(this);
    m_matchCaseButton->setObjectName(QStringLiteral("mediaViewerMatchCaseButton"));
    m_matchCaseButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_matchCaseButton->setText(tr("Aa"));
    m_matchCaseButton->setCheckable(true);
    m_matchCaseButton->setToolTip(tr("Match Case\nE.g. \"File\" will not match \"file\""));
    connect(m_matchCaseButton, &QToolButton::toggled, this, &MediaViewerSearchPanelWidget::emitNameFilterChanged);

    m_wholeWordButton = new QToolButton(this);
    m_wholeWordButton->setObjectName(QStringLiteral("mediaViewerWholeWordButton"));
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(
        tr("Match Whole Word\nMatches whole word only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\", but will match \"File Name\""));
    connect(m_wholeWordButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_wholeWordButton, checked); });

    m_exactNameButton = new QToolButton(this);
    m_exactNameButton->setObjectName(QStringLiteral("mediaViewerExactNameButton"));
    m_exactNameButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_exactNameButton->setText(tr("="));
    m_exactNameButton->setCheckable(true);
    m_exactNameButton->setToolTip(
        tr("Exact Name\nMatches exact name only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\" nor \"File Name\", but will match only \"file\" or \"File\""));
    connect(m_exactNameButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_exactNameButton, checked); });

    m_regexButton = new QToolButton(this);
    m_regexButton->setObjectName(QStringLiteral("mediaViewerRegexButton"));
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));
    connect(m_regexButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_regexButton, checked); });

    m_skipGitIgnoredButton = new QToolButton(this);
    m_skipGitIgnoredButton->setObjectName(QStringLiteral("mediaViewerSkipGitIgnoredButton"));
    m_skipGitIgnoredButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_skipGitIgnoredButton->setText(tr(".gi"));
    m_skipGitIgnoredButton->setCheckable(true);
    m_skipGitIgnoredButton->setToolTip(
        tr("Skip Git-Ignored\nSkips files excluded by .gitignore (has no effect in Media Viewer mode, which never re-scans on this toggle)"));
    connect(m_skipGitIgnoredButton, &QToolButton::toggled, this, &MediaViewerSearchPanelWidget::emitNameFilterChanged);

    auto* matchOptionsRow = new QHBoxLayout();
    matchOptionsRow->addWidget(m_matchCaseButton);
    matchOptionsRow->addWidget(m_wholeWordButton);
    matchOptionsRow->addWidget(m_exactNameButton);
    matchOptionsRow->addWidget(m_regexButton);
    matchOptionsRow->addWidget(m_skipGitIgnoredButton);
    matchOptionsRow->addStretch();

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("mediaViewerNameEdit"));
    m_nameEdit->setPlaceholderText(tr("Name contains... (*, ? wildcards)"));

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->setSpacing(10);
    
    layout->addSpacing(20);

    m_appliedFiltersLayout = addChipSection(layout, tr("Tag filters:"), &UiColors::UiColorSet::selectedItemTagsBackground,
                                             QStringLiteral("mediaViewerAppliedFiltersSection"));

    layout->addSpacing(20);

    layout->addLayout(tagSearchRow);
    layout->addWidget(m_createTagButton);
    m_tagMatchesLayout = addChipSection(layout, QString(), &UiColors::UiColorSet::matchingTagsBackground,
                                         QStringLiteral("mediaViewerTagMatchesSection"));

    layout->addSpacing(20);

    layout->addLayout(matchOptionsRow);
    layout->addWidget(m_nameEdit);
    // Now that both chip sections are a fixed height rather than Expanding,
    // we can add a stretch at the bottom to push everything up to the top of the panel.
    layout->addStretch();

    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        MediaViewerKind kind = MediaViewerKind::Image;
        if (index == 1)
            kind = MediaViewerKind::Video;
        else if (index == 2)
            kind = MediaViewerKind::Animation;
        emit kindChanged(kind);
    });
    connect(m_tagBehaviourCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        emit tagBehaviourChanged(index == 1 ? MediaViewerTagBehaviour::Apply : MediaViewerTagBehaviour::Filter);
    });
    connect(m_exitButton, &QToolButton::clicked, this, &MediaViewerSearchPanelWidget::exitRequested);
    connect(m_tagSearchEdit, &QLineEdit::textChanged, this, &MediaViewerSearchPanelWidget::tagSearchQueryRequested);
    connect(m_tagSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        emit tagSearchQueryAccepted(m_tagSearchEdit->text());
        m_tagSearchEdit->clear();
    });
    connect(m_createTagButton, &QPushButton::clicked, this, [this]() {
        emit tagCreateRequested(m_tagSearchEdit->text().trimmed());
        m_tagSearchEdit->clear();
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString&) { emitNameFilterChanged(); });

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &MediaViewerSearchPanelWidget::applyThemeColors);
}

FlowLayout* MediaViewerSearchPanelWidget::addChipSection(QVBoxLayout* mainLayout, const QString& title, ColorRole colorRole,
                                                           const QString& sectionObjectName)
{
    if (!title.isEmpty())
    {
        mainLayout->addWidget(new QLabel(title, this));
    }

    auto* content = new QWidget;
    content->setObjectName(sectionObjectName);
    content->setAttribute(Qt::WA_StyledBackground, true);
    auto* contentLayout = new FlowLayout(content);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setAttribute(Qt::WA_StyledBackground, true);
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; }"));
   
    scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    scrollArea->setFixedHeight(120);
    mainLayout->addWidget(scrollArea);

    m_themedSections.push_back(ThemedSection{ content, scrollArea->viewport(), colorRole });

    return contentLayout;
}

void MediaViewerSearchPanelWidget::applyThemeColors()
{
    const UiColors::UiColorSet& colors = UiColors::current();
    for (const ThemedSection& section : m_themedSections)
    {
        const QString style = QString("background-color: %1;").arg(QLatin1String(colors.*section.colorRole));
        section.content->setStyleSheet(style);
        section.viewport->setStyleSheet(style);
    }
}

void MediaViewerSearchPanelWidget::setKind(MediaViewerKind kind)
{
    const QSignalBlocker blocker(m_kindCombo);
    int index = 0;
    if (kind == MediaViewerKind::Video)
        index = 1;
    else if (kind == MediaViewerKind::Animation)
        index = 2;
    m_kindCombo->setCurrentIndex(index);
}

void MediaViewerSearchPanelWidget::setAppliedTagFilters(const std::vector<Tag>& tags)
{
    clearLayout(m_appliedFiltersLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Removable);
        connect(chip, &TagChipWidget::removeClicked, this, &MediaViewerSearchPanelWidget::tagFilterRemoved);
        m_appliedFiltersLayout->addWidget(chip);
    }
}

void MediaViewerSearchPanelWidget::setTagSearchResults(const std::vector<Tag>& tags)
{
    clearLayout(m_tagMatchesLayout);
    QStringList names;
    names.reserve(static_cast<int>(tags.size()));
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::ReadOnly);
        connect(chip, &TagChipWidget::clicked, this, &MediaViewerSearchPanelWidget::tagSearchChipActivated);
        m_tagMatchesLayout->addWidget(chip);
        names.append(QString::fromStdString(tag.name()));
    }
    m_tagSearchEdit->setSuggestions(names);
    updateCreateTagAffordance(tags);
}

void MediaViewerSearchPanelWidget::setTagBehaviour(MediaViewerTagBehaviour behaviour)
{
    const QSignalBlocker blocker(m_tagBehaviourCombo);
    m_tagBehaviourCombo->setCurrentIndex(behaviour == MediaViewerTagBehaviour::Apply ? 1 : 0);
}

void MediaViewerSearchPanelWidget::setNameFilter(const QString& query, const NameSearchOptions& options)
{
    const QSignalBlocker nameBlocker(m_nameEdit);
    const QSignalBlocker caseBlocker(m_matchCaseButton);
    const QSignalBlocker wholeWordBlocker(m_wholeWordButton);
    const QSignalBlocker exactNameBlocker(m_exactNameButton);
    const QSignalBlocker regexBlocker(m_regexButton);
    const QSignalBlocker skipGitIgnoredBlocker(m_skipGitIgnoredButton);

    if (!m_nameEdit->hasFocus())
    {
        m_nameEdit->setText(query);
    }
    m_matchCaseButton->setChecked(options.caseSensitive);
    m_wholeWordButton->setChecked(options.mode == NameMatchMode::WholeWord);
    m_exactNameButton->setChecked(options.mode == NameMatchMode::ExactName);
    m_regexButton->setChecked(options.mode == NameMatchMode::Regex);
    m_skipGitIgnoredButton->setChecked(options.skipGitIgnored);
}

void MediaViewerSearchPanelWidget::setLoading(bool loading)
{
    m_loadingLabel->setVisible(loading);
}

void MediaViewerSearchPanelWidget::updateCreateTagAffordance(const std::vector<Tag>& matches)
{
    const QString trimmed = m_tagSearchEdit->text().trimmed();
    const bool canCreate = !trimmed.isEmpty() &&
        std::none_of(matches.begin(), matches.end(), [&trimmed](const Tag& tag) {
            return QString::fromStdString(tag.name()).compare(trimmed, Qt::CaseInsensitive) == 0;
        });

    m_createTagButton->setVisible(canCreate);
    if (canCreate)
    {
        m_createTagButton->setText(tr("Create tag \"%1\"").arg(trimmed));
    }
}

void MediaViewerSearchPanelWidget::clearLayout(FlowLayout* layout)
{
    while (layout->count() > 0)
    {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }
}

NameSearchOptions MediaViewerSearchPanelWidget::currentNameOptions() const
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
    options.skipGitIgnored = m_skipGitIgnoredButton->isChecked();
    return options;
}

void MediaViewerSearchPanelWidget::emitNameFilterChanged()
{
    emit nameFilterChanged(m_nameEdit->text().trimmed(), currentNameOptions());
}

void MediaViewerSearchPanelWidget::onMatchModeButtonToggled(QToolButton* button, bool checked)
{
    applyExclusiveToggle(button, checked, { m_wholeWordButton, m_exactNameButton, m_regexButton });
    emitNameFilterChanged();
}
