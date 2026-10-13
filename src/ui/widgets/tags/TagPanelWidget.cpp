#include "TagPanelWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>

#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "TagListViewModel.h"
#include "TagSearchEdit.h"
#include "UiThemeChangeNotifier.h"

TagPanelWidget::TagPanelWidget(TagListViewModel* viewModel, UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    auto* mainLayout = new QVBoxLayout(this);

    m_folderTagsLayout = addChipSection(mainLayout, tr("Folder tags"), &UiColors::UiColorSet::folderTagsBackground,
                                         QStringLiteral("tagPanelFolderTagsSection"));

    m_searchEdit = new TagSearchEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("tagPanelSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("Search tags..."));
    mainLayout->addWidget(m_searchEdit);

    m_searchResultsLayout = addChipSection(mainLayout, tr("Matching tags"), &UiColors::UiColorSet::matchingTagsBackground,
                                            QStringLiteral("tagPanelSearchMatchesSection"));

    m_createTagButton = new QPushButton(this);
    m_createTagButton->setObjectName(QStringLiteral("tagPanelCreateTagButton"));
    m_createTagButton->hide();
    mainLayout->addWidget(m_createTagButton);

    m_selectedItemTagsCombineModeCombo = new QComboBox(this);
    m_selectedItemTagsCombineModeCombo->setObjectName(QStringLiteral("tagPanelSelectedItemTagsCombineModeCombo"));
    m_selectedItemTagsCombineModeCombo->addItem(tr("AND"));
    m_selectedItemTagsCombineModeCombo->addItem(tr("OR"));

    m_selectedItemTagsLayout = addChipSection(mainLayout, tr("Tags on selection"), &UiColors::UiColorSet::selectedItemTagsBackground,
                                               QStringLiteral("tagPanelSelectedItemTagsSection"), m_selectedItemTagsCombineModeCombo);

    connect(m_searchEdit, &QLineEdit::textChanged, m_viewModel, &TagListViewModel::setSearchQuery);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &TagPanelWidget::onSearchEditReturnPressed);
    connect(m_createTagButton, &QPushButton::clicked, m_viewModel, &TagListViewModel::createAndAddTagFromQuery);
    connect(m_selectedItemTagsCombineModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_viewModel->setSelectedItemTagsCombineMode(static_cast<TagListViewModel::CombineMode>(index));
    });

    connect(m_viewModel, &TagListViewModel::folderTagsChanged, this, &TagPanelWidget::rebuildFolderTags);
    connect(m_viewModel, &TagListViewModel::searchResultsChanged, this, &TagPanelWidget::rebuildSearchResults);
    connect(m_viewModel, &TagListViewModel::searchResultsChanged, this, &TagPanelWidget::updateSearchSuggestions);
    connect(m_viewModel, &TagListViewModel::selectedItemTagsChanged, this, &TagPanelWidget::rebuildSelectedItemTags);
    connect(m_viewModel, &TagListViewModel::canCreateTagFromQueryChanged, this, &TagPanelWidget::updateCreateTagAffordance);

    rebuildFolderTags(m_viewModel->folderTags());
    rebuildSearchResults(m_viewModel->searchResults());
    updateSearchSuggestions(m_viewModel->searchResults());
    rebuildSelectedItemTags(m_viewModel->selectedItemTags());
    updateCreateTagAffordance(m_viewModel->canCreateTagFromQuery());

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &TagPanelWidget::applyThemeColors);
}

FlowLayout* TagPanelWidget::addChipSection(QVBoxLayout* mainLayout, const QString& title, ColorRole colorRole,
                                            const QString& sectionObjectName, QWidget* headerExtraWidget)
{
    if (headerExtraWidget)
    {
        auto* headerLayout = new QHBoxLayout;
        headerLayout->addWidget(new QLabel(title, this), 1);
        headerLayout->addWidget(headerExtraWidget);
        mainLayout->addLayout(headerLayout);
    }
    else
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
    mainLayout->addWidget(scrollArea);

    m_themedSections.push_back(ThemedSection{ content, scrollArea->viewport(), colorRole });

    return contentLayout;
}

void TagPanelWidget::applyThemeColors()
{
    const UiColors::UiColorSet& colors = UiColors::current();
    for (const ThemedSection& section : m_themedSections)
    {
        const QString style = QString("background-color: %1;").arg(QLatin1String(colors.*section.colorRole));
        section.content->setStyleSheet(style);
        section.viewport->setStyleSheet(style);
    }
}

void TagPanelWidget::clearLayout(FlowLayout* layout)
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

void TagPanelWidget::rebuildFolderTags(const std::vector<Tag>& tags)
{
    clearLayout(m_folderTagsLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::ReadOnly);
        connect(chip, &TagChipWidget::clicked, this, &TagPanelWidget::onTagChipClicked);
        m_folderTagsLayout->addWidget(chip);
    }
}

void TagPanelWidget::rebuildSearchResults(const std::vector<Tag>& tags)
{
    clearLayout(m_searchResultsLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Addable);
        connect(chip, &TagChipWidget::addClicked, m_viewModel, &TagListViewModel::addTagToSelection);
        connect(chip, &TagChipWidget::clicked, this, &TagPanelWidget::onTagChipClicked);
        m_searchResultsLayout->addWidget(chip);
    }
}

void TagPanelWidget::rebuildSelectedItemTags(const std::vector<Tag>& tags)
{
    clearLayout(m_selectedItemTagsLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Removable);
        connect(chip, &TagChipWidget::removeClicked, m_viewModel, &TagListViewModel::removeTagFromSelection);
        connect(chip, &TagChipWidget::clicked, this, &TagPanelWidget::onTagChipClicked);
        m_selectedItemTagsLayout->addWidget(chip);
    }
}

void TagPanelWidget::onTagChipClicked(Tag::Id tagId)
{
    m_viewModel->requestTagSearch(tagId);
}

void TagPanelWidget::onSearchEditReturnPressed()
{
    m_viewModel->acceptSearchQuery();
    m_searchEdit->clear();
}

void TagPanelWidget::updateSearchSuggestions(const std::vector<Tag>& tags)
{
    QStringList names;
    names.reserve(static_cast<int>(tags.size()));
    for (const Tag& tag : tags)
    {
        names.append(QString::fromStdString(tag.name()));
    }
    m_searchEdit->setSuggestions(names);
}

void TagPanelWidget::updateCreateTagAffordance(bool canCreate)
{
    m_createTagButton->setVisible(canCreate);
    if (canCreate)
    {
        m_createTagButton->setText(tr("Create tag \"%1\"").arg(m_viewModel->searchQuery().trimmed()));
    }
}
