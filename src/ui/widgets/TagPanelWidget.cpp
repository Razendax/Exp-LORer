#include "TagPanelWidget.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "TagListViewModel.h"

TagPanelWidget::TagPanelWidget(TagListViewModel* viewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    auto* mainLayout = new QVBoxLayout(this);

    m_folderTagsLayout = addChipSection(mainLayout, tr("Folder tags"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search tags..."));
    mainLayout->addWidget(m_searchEdit);

    m_searchResultsLayout = addChipSection(mainLayout, tr("Matching tags"));

    m_createTagButton = new QPushButton(this);
    m_createTagButton->hide();
    mainLayout->addWidget(m_createTagButton);

    m_selectedItemTagsLayout = addChipSection(mainLayout, tr("Tags on selection"));

    connect(m_searchEdit, &QLineEdit::textChanged, m_viewModel, &TagListViewModel::setSearchQuery);
    connect(m_createTagButton, &QPushButton::clicked, m_viewModel, &TagListViewModel::createAndAddTagFromQuery);

    connect(m_viewModel, &TagListViewModel::folderTagsChanged, this, &TagPanelWidget::rebuildFolderTags);
    connect(m_viewModel, &TagListViewModel::searchResultsChanged, this, &TagPanelWidget::rebuildSearchResults);
    connect(m_viewModel, &TagListViewModel::selectedItemTagsChanged, this, &TagPanelWidget::rebuildSelectedItemTags);
    connect(m_viewModel, &TagListViewModel::canCreateTagFromQueryChanged, this, &TagPanelWidget::updateCreateTagAffordance);

    rebuildFolderTags(m_viewModel->folderTags());
    rebuildSearchResults(m_viewModel->searchResults());
    rebuildSelectedItemTags(m_viewModel->selectedItemTags());
    updateCreateTagAffordance(m_viewModel->canCreateTagFromQuery());
}

FlowLayout* TagPanelWidget::addChipSection(QVBoxLayout* mainLayout, const QString& title)
{
    mainLayout->addWidget(new QLabel(title, this));

    auto* content = new QWidget;
    auto* contentLayout = new FlowLayout(content);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mainLayout->addWidget(scrollArea);

    return contentLayout;
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

void TagPanelWidget::updateCreateTagAffordance(bool canCreate)
{
    m_createTagButton->setVisible(canCreate);
    if (canCreate)
    {
        m_createTagButton->setText(tr("Create tag \"%1\"").arg(m_viewModel->searchQuery().trimmed()));
    }
}
