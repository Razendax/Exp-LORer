#include "SearchCriteriaPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "UiColors.h"
#include "UiThemeChangeNotifier.h"

namespace
{
    constexpr int kMaxSizeValue = 2000000000;
    constexpr std::uintmax_t kBytesPerKb = 1024;
    // Indexed by the unit combo's currentIndex() -- order matches the addItems() call below.
    constexpr std::uintmax_t kSizeUnitMultipliers[] = { kBytesPerKb, kBytesPerKb * kBytesPerKb, kBytesPerKb * kBytesPerKb * kBytesPerKb };
    // Same debounce interval as SearchPanelWidget's quick-search box (kSearchDebounceMs).
    constexpr int kCriteriaDebounceMs = 500;
}

SearchCriteriaPanel::SearchCriteriaPanel(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("searchCriteriaPanel"));

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("searchNameEdit"));
    m_nameEdit->setPlaceholderText(tr("Name contains... (*, ? wildcards)"));

    m_minSizeSpin = new QSpinBox(this);
    m_minSizeSpin->setObjectName(QStringLiteral("searchMinSizeSpin"));
    m_minSizeSpin->setRange(0, kMaxSizeValue);
    m_minSizeSpin->setSpecialValueText(tr("Any"));

    m_minSizeUnitCombo = new QComboBox(this);
    m_minSizeUnitCombo->setObjectName(QStringLiteral("searchMinSizeUnitCombo"));
    m_minSizeUnitCombo->addItems({ tr("KB"), tr("MB"), tr("GB") });

    m_maxSizeSpin = new QSpinBox(this);
    m_maxSizeSpin->setObjectName(QStringLiteral("searchMaxSizeSpin"));
    m_maxSizeSpin->setRange(0, kMaxSizeValue);
    m_maxSizeSpin->setSpecialValueText(tr("Any"));

    m_maxSizeUnitCombo = new QComboBox(this);
    m_maxSizeUnitCombo->setObjectName(QStringLiteral("searchMaxSizeUnitCombo"));
    m_maxSizeUnitCombo->addItems({ tr("KB"), tr("MB"), tr("GB") });

    m_searchButton = new QPushButton(tr("Search"), this);
    m_searchButton->setObjectName(QStringLiteral("searchSubmitButton"));

    m_closeButton = new QToolButton(this);
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setToolTip(tr("Close advanced search"));
    m_closeButton->setObjectName(QStringLiteral("advancedSearchCloseButton"));

    auto* fieldsRow = new QHBoxLayout();
    fieldsRow->addWidget(new QLabel(tr("Name:"), this));
    fieldsRow->addWidget(m_nameEdit, 2);
    fieldsRow->addWidget(new QLabel(tr("Min size:"), this));
    fieldsRow->addWidget(m_minSizeSpin);
    fieldsRow->addWidget(m_minSizeUnitCombo);
    fieldsRow->addWidget(new QLabel(tr("Max size:"), this));
    fieldsRow->addWidget(m_maxSizeSpin);
    fieldsRow->addWidget(m_maxSizeUnitCombo);
    fieldsRow->addWidget(m_searchButton);
    fieldsRow->addWidget(m_closeButton);

    // Match Case/Whole Word/Exact Name/Match Regex/Skip Git-Ignored toggles (Architecture.md
    // §14.56) -- same text-only buttons/tooltips as SearchPanelWidget's equivalent row.
    m_matchCaseButton = new QToolButton(this);
    m_matchCaseButton->setObjectName(QStringLiteral("advancedSearchMatchCaseButton"));
    m_matchCaseButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_matchCaseButton->setText(tr("Aa"));
    m_matchCaseButton->setCheckable(true);
    m_matchCaseButton->setToolTip(tr("Match Case\nE.g. \"File\" will not match \"file\""));
    connect(m_matchCaseButton, &QToolButton::toggled, this, &SearchCriteriaPanel::onAnyFieldEdited);

    m_wholeWordButton = new QToolButton(this);
    m_wholeWordButton->setObjectName(QStringLiteral("advancedSearchWholeWordButton"));
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(
        tr("Match Whole Word\nMatches whole word only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\", but will match \"File Name\""));
    connect(m_wholeWordButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_wholeWordButton, checked); });

    m_exactNameButton = new QToolButton(this);
    m_exactNameButton->setObjectName(QStringLiteral("advancedSearchExactNameButton"));
    m_exactNameButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_exactNameButton->setText(tr("="));
    m_exactNameButton->setCheckable(true);
    m_exactNameButton->setToolTip(
        tr("Exact Name\nMatches exact name only, not in between. Case insensitive.\n"
           "E.g. \"File\" will not match \"FileName\" nor \"File Name\", but will match only \"file\" or \"File\""));
    connect(m_exactNameButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_exactNameButton, checked); });

    m_regexButton = new QToolButton(this);
    m_regexButton->setObjectName(QStringLiteral("advancedSearchRegexButton"));
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));
    connect(m_regexButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_regexButton, checked); });

    m_skipGitIgnoredButton = new QToolButton(this);
    m_skipGitIgnoredButton->setObjectName(QStringLiteral("advancedSearchSkipGitIgnoredButton"));
    m_skipGitIgnoredButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_skipGitIgnoredButton->setText(tr(".gi"));
    m_skipGitIgnoredButton->setCheckable(true);
    m_skipGitIgnoredButton->setToolTip(
        tr("Skip Git-Ignored\nSkips files and folders excluded by .gitignore when searching inside a git working tree"));
    connect(m_skipGitIgnoredButton, &QToolButton::toggled, this, &SearchCriteriaPanel::onAnyFieldEdited);

    auto* matchOptionsRow = new QHBoxLayout();
    matchOptionsRow->addWidget(m_matchCaseButton);
    matchOptionsRow->addWidget(m_wholeWordButton);
    matchOptionsRow->addWidget(m_exactNameButton);
    matchOptionsRow->addWidget(m_regexButton);
    matchOptionsRow->addWidget(m_skipGitIgnoredButton);
    matchOptionsRow->addStretch();

    m_tagSearchEdit = new QLineEdit(this);
    m_tagSearchEdit->setObjectName(QStringLiteral("searchTagEdit"));
    m_tagSearchEdit->setPlaceholderText(tr("Search tags..."));

    m_tagIncludeChildrenButton = new QToolButton(this);
    m_tagIncludeChildrenButton->setObjectName(QStringLiteral("advancedSearchTagIncludeChildrenButton"));
    m_tagIncludeChildrenButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tagIncludeChildrenButton->setText(tr("+ch"));
    m_tagIncludeChildrenButton->setCheckable(true);
    m_tagIncludeChildrenButton->setToolTip(
        tr("Include nested child tags\nE.g. selecting \"game\" also matches \"game/action\""));
    connect(m_tagIncludeChildrenButton, &QToolButton::toggled, this, &SearchCriteriaPanel::onAnyFieldEdited);

    m_tagMatchesLayout = new FlowLayout();

    auto* tagMatchesRow = new QHBoxLayout();
    tagMatchesRow->addWidget(m_tagSearchEdit);
    tagMatchesRow->addWidget(m_tagIncludeChildrenButton);
    tagMatchesRow->addLayout(m_tagMatchesLayout, 1);

    m_tagCriteriaLayout = new FlowLayout();
    auto* tagCriteriaRow = new QHBoxLayout();
    tagCriteriaRow->addWidget(new QLabel(tr("Tag search criteria:"), this));
    tagCriteriaRow->addLayout(m_tagCriteriaLayout, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(fieldsRow);
    layout->addLayout(matchOptionsRow);
    layout->addLayout(tagMatchesRow);
    layout->addLayout(tagCriteriaRow);

    m_criteriaDebounceTimer = new QTimer(this);
    m_criteriaDebounceTimer->setSingleShot(true);
    m_criteriaDebounceTimer->setInterval(kCriteriaDebounceMs);
    connect(m_criteriaDebounceTimer, &QTimer::timeout, this, &SearchCriteriaPanel::onAnyFieldEdited);

    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() {
        m_criteriaDebounceTimer->stop();
        emit searchRequested(currentCriteria());
    });
    connect(m_searchButton, &QPushButton::clicked, this, [this]() {
        m_criteriaDebounceTimer->stop();
        emit searchRequested(currentCriteria());
    });
    connect(m_closeButton, &QToolButton::clicked, this, &SearchCriteriaPanel::closeRequested);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString& text) { scheduleCriteriaEdited(text.trimmed().isEmpty()); });
    connect(m_minSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { scheduleCriteriaEdited(); });
    connect(m_maxSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { scheduleCriteriaEdited(); });
    // Unit changes are a discrete action (like the toggle buttons), not continuous typing -- no debounce.
    connect(m_minSizeUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_maxSizeUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);

    connect(m_tagSearchEdit, &QLineEdit::textChanged, this, &SearchCriteriaPanel::tagSearchQueryRequested);

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &SearchCriteriaPanel::applyThemeColors);
}

void SearchCriteriaPanel::applyThemeColors()
{
    const UiColors::UiColorSet& colors = UiColors::current();
    m_closeButton->setStyleSheet(QStringLiteral(
        "QToolButton#advancedSearchCloseButton { color: white; background: %1; border: none; border-radius: 3px; padding: 2px 6px; }"
        "QToolButton#advancedSearchCloseButton:hover { background: %2; }")
        .arg(QLatin1String(colors.closeButtonRed), QLatin1String(colors.closeButtonRedHover)));
}

void SearchCriteriaPanel::setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags)
{
    // Discard any pending debounced edit -- it would otherwise fire later with field values this
    // external resync is about to replace.
    m_criteriaDebounceTimer->stop();

    const QSignalBlocker nameBlocker(m_nameEdit);
    const QSignalBlocker minBlocker(m_minSizeSpin);
    const QSignalBlocker maxBlocker(m_maxSizeSpin);
    const QSignalBlocker caseBlocker(m_matchCaseButton);
    const QSignalBlocker wholeWordBlocker(m_wholeWordButton);
    const QSignalBlocker exactNameBlocker(m_exactNameButton);
    const QSignalBlocker regexBlocker(m_regexButton);
    const QSignalBlocker skipGitIgnoredBlocker(m_skipGitIgnoredButton);
    const QSignalBlocker tagIncludeChildrenBlocker(m_tagIncludeChildrenButton);

    // Skip a field that currently has focus: this call is also reached via the round trip of the
    // user's own edit (onAnyFieldEdited -> criteriaEdited -> TabViewModel::updateAdvancedSearchCriteria
    // -> advancedSearchCriteriaChanged -> back here), and re-applying setText/setValue while the user
    // is still typing in that field would reset the cursor to the end on every keystroke and, since
    // currentCriteria() trims the name, would also erase a trailing space as soon as it's typed.
    if (!m_nameEdit->hasFocus())
    {
        m_nameEdit->setText(QString::fromStdString(criteria.nameQuery));
    }
    // The unit combo is sticky UI-only state -- recompute the spin box's number against whatever
    // unit is currently selected rather than resetting the combo itself.
    if (!m_minSizeSpin->hasFocus())
    {
        const std::uintmax_t divisor = kSizeUnitMultipliers[m_minSizeUnitCombo->currentIndex()];
        m_minSizeSpin->setValue(criteria.minSizeBytes ? static_cast<int>(*criteria.minSizeBytes / divisor) : 0);
    }
    if (!m_maxSizeSpin->hasFocus())
    {
        const std::uintmax_t divisor = kSizeUnitMultipliers[m_maxSizeUnitCombo->currentIndex()];
        m_maxSizeSpin->setValue(criteria.maxSizeBytes ? static_cast<int>(*criteria.maxSizeBytes / divisor) : 0);
    }
    m_matchCaseButton->setChecked(criteria.nameOptions.caseSensitive);
    m_wholeWordButton->setChecked(criteria.nameOptions.mode == NameMatchMode::WholeWord);
    m_exactNameButton->setChecked(criteria.nameOptions.mode == NameMatchMode::ExactName);
    m_regexButton->setChecked(criteria.nameOptions.mode == NameMatchMode::Regex);
    m_skipGitIgnoredButton->setChecked(criteria.nameOptions.skipGitIgnored);
    m_tagIncludeChildrenButton->setChecked(criteria.tagIncludeChildTags);
    m_lastKnownTagIds = criteria.tagIds;

    clearLayout(m_tagCriteriaLayout);
    for (const Tag& tag : criteriaTags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Removable);
        connect(chip, &TagChipWidget::removeClicked, this, &SearchCriteriaPanel::tagCriterionRemoved);
        m_tagCriteriaLayout->addWidget(chip);
    }
}

void SearchCriteriaPanel::setTagSearchResults(const std::vector<Tag>& tags)
{
    clearLayout(m_tagMatchesLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::ReadOnly);
        connect(chip, &TagChipWidget::clicked, this, &SearchCriteriaPanel::tagCriterionAdded);
        m_tagMatchesLayout->addWidget(chip);
    }
}

void SearchCriteriaPanel::setSearching(bool searching)
{
    // Stays enabled throughout -- clicking Search (or editing a field) while a previous search is
    // still running is exactly how the user restarts it with the latest criteria rather than
    // waiting it out (Architecture.md §14.37).
    m_searchButton->setText(searching ? tr("Searching…") : tr("Search"));
}

void SearchCriteriaPanel::clearLayout(FlowLayout* layout)
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

SearchCriteria SearchCriteriaPanel::currentCriteria() const
{
    SearchCriteria criteria;
    criteria.nameQuery = m_nameEdit->text().trimmed().toStdString();
    if (m_minSizeSpin->value() > 0)
    {
        criteria.minSizeBytes = static_cast<std::uintmax_t>(m_minSizeSpin->value()) * kSizeUnitMultipliers[m_minSizeUnitCombo->currentIndex()];
    }
    if (m_maxSizeSpin->value() > 0)
    {
        criteria.maxSizeBytes = static_cast<std::uintmax_t>(m_maxSizeSpin->value()) * kSizeUnitMultipliers[m_maxSizeUnitCombo->currentIndex()];
    }
    criteria.tagIds = m_lastKnownTagIds;
    criteria.tagIncludeChildTags = m_tagIncludeChildrenButton->isChecked();

    criteria.nameOptions.caseSensitive = m_matchCaseButton->isChecked();
    if (m_wholeWordButton->isChecked())
    {
        criteria.nameOptions.mode = NameMatchMode::WholeWord;
    }
    else if (m_exactNameButton->isChecked())
    {
        criteria.nameOptions.mode = NameMatchMode::ExactName;
    }
    else if (m_regexButton->isChecked())
    {
        criteria.nameOptions.mode = NameMatchMode::Regex;
    }
    criteria.nameOptions.skipGitIgnored = m_skipGitIgnoredButton->isChecked();

    return criteria;
}

void SearchCriteriaPanel::onAnyFieldEdited()
{
    m_criteriaDebounceTimer->stop();
    emit criteriaEdited(currentCriteria());
}

void SearchCriteriaPanel::scheduleCriteriaEdited(bool bypassDebounceIfTextEmpty)
{
    if (bypassDebounceIfTextEmpty)
    {
        onAnyFieldEdited();
    }
    else
    {
        m_criteriaDebounceTimer->start();
    }
}

void SearchCriteriaPanel::onMatchModeButtonToggled(QToolButton* button, bool checked)
{
    if (checked)
    {
        // Manual "exclusive but deselectable" behavior -- see SearchPanelWidget::
        // onMatchModeButtonToggled, ported here verbatim for the same vendored-Qt
        // QButtonGroup limitation.
        for (QToolButton* other : { m_wholeWordButton, m_exactNameButton, m_regexButton })
        {
            if (other != button && other->isChecked())
            {
                const QSignalBlocker blocker(other);
                other->setChecked(false);
            }
        }
    }

    onAnyFieldEdited();
}
