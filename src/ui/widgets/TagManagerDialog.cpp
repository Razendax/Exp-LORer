#include "TagManagerDialog.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "TagManagerViewModel.h"

TagManagerDialog::TagManagerDialog(TagManagerViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setWindowTitle(tr("Tag Manager"));

    auto* mainLayout = new QVBoxLayout(this);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search tags..."));
    mainLayout->addWidget(m_searchEdit);

    auto* chipContent = new QWidget;
    m_chipLayout = new FlowLayout(chipContent);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(chipContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mainLayout->addWidget(scrollArea);

    auto* buttonLayout = new QHBoxLayout();
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_addButton = new QPushButton(tr("Add"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    m_renameButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_searchEdit, &QLineEdit::textChanged, m_viewModel, &TagManagerViewModel::setSearchQuery);
    connect(m_addButton, &QPushButton::clicked, this, &TagManagerDialog::onAddClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &TagManagerDialog::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &TagManagerDialog::onDeleteClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_viewModel, &TagManagerViewModel::matchingTagsChanged, this, &TagManagerDialog::rebuildTagChips);
    connect(m_viewModel, &TagManagerViewModel::operationFailed, this, &TagManagerDialog::onOperationFailed);

    rebuildTagChips(m_viewModel->matchingTags());
}

void TagManagerDialog::rebuildTagChips(const std::vector<Tag>& tags)
{
    while (m_chipLayout->count() > 0)
    {
        QLayoutItem* item = m_chipLayout->takeAt(0);
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }

    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Selectable);
        connect(chip, &TagChipWidget::clicked, this, &TagManagerDialog::onChipClicked);
        m_chipLayout->addWidget(chip);
    }

    m_selectedTagId.reset();
    m_renameButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
}

void TagManagerDialog::onChipClicked(Tag::Id id)
{
    for (int i = 0; i < m_chipLayout->count(); ++i)
    {
        if (auto* chip = qobject_cast<TagChipWidget*>(m_chipLayout->itemAt(i)->widget()))
        {
            chip->setSelected(chip->tag().id() == id);
        }
    }

    m_selectedTagId = id;
    m_renameButton->setEnabled(true);
    m_deleteButton->setEnabled(true);
}

void TagManagerDialog::onAddClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add Tag"), tr("Tag name:"), QLineEdit::Normal,
                                                m_searchEdit->text().trimmed(), &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }

    m_viewModel->addTag(name.trimmed());
}

void TagManagerDialog::onRenameClicked()
{
    if (!m_selectedTagId.has_value())
    {
        return;
    }

    const std::vector<Tag>& tags = m_viewModel->matchingTags();
    const auto it = std::find_if(tags.begin(), tags.end(),
                                  [id = *m_selectedTagId](const Tag& tag) { return tag.id() == id; });
    if (it == tags.end())
    {
        return;
    }

    const QString currentName = QString::fromStdString(it->name());

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Tag"), tr("New name:"), QLineEdit::Normal,
                                                    currentName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == currentName)
    {
        return;
    }

    m_viewModel->renameTag(*m_selectedTagId, newName.trimmed());
}

void TagManagerDialog::onDeleteClicked()
{
    if (!m_selectedTagId.has_value())
    {
        return;
    }

    const std::vector<Tag>& tags = m_viewModel->matchingTags();
    const auto it = std::find_if(tags.begin(), tags.end(),
                                  [id = *m_selectedTagId](const Tag& tag) { return tag.id() == id; });
    if (it == tags.end())
    {
        return;
    }

    const QString name = QString::fromStdString(it->name());

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Delete Tag"),
                               tr("Delete tag \"%1\"? It will be removed from every file and folder it is applied to.")
                                   .arg(name));
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    m_viewModel->deleteTag(*m_selectedTagId);
}

void TagManagerDialog::onOperationFailed(const QString& message)
{
    QMessageBox::warning(this, tr("Tag Manager"), message);
}
