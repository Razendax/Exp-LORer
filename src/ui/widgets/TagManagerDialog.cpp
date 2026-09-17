#include "TagManagerDialog.h"

#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "TagManagerViewModel.h"

namespace
{
    constexpr int kTagIdRole = Qt::UserRole;

    QIcon colorSwatch(const std::string& hexColor)
    {
        QPixmap pixmap(16, 16);
        pixmap.fill(QColor(QString::fromStdString(hexColor)));
        return QIcon(pixmap);
    }
}

TagManagerDialog::TagManagerDialog(TagManagerViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setWindowTitle(tr("Tag Manager"));

    auto* mainLayout = new QVBoxLayout(this);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search tags..."));
    mainLayout->addWidget(m_searchEdit);

    m_tagList = new QListWidget(this);
    m_tagList->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_tagList);

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
    connect(m_tagList, &QListWidget::itemSelectionChanged, this, &TagManagerDialog::onSelectionChanged);
    connect(m_addButton, &QPushButton::clicked, this, &TagManagerDialog::onAddClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &TagManagerDialog::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &TagManagerDialog::onDeleteClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_viewModel, &TagManagerViewModel::matchingTagsChanged, this, &TagManagerDialog::rebuildTagList);
    connect(m_viewModel, &TagManagerViewModel::operationFailed, this, &TagManagerDialog::onOperationFailed);

    rebuildTagList(m_viewModel->matchingTags());
}

void TagManagerDialog::rebuildTagList(const std::vector<Tag>& tags)
{
    m_tagList->clear();
    for (const Tag& tag : tags)
    {
        auto* item = new QListWidgetItem(colorSwatch(tag.hexColor()), QString::fromStdString(tag.name()));
        item->setData(kTagIdRole, QVariant::fromValue<qlonglong>(tag.id()));
        m_tagList->addItem(item);
    }
}

void TagManagerDialog::onSelectionChanged()
{
    const bool hasSingleSelection = m_tagList->selectedItems().size() == 1;
    m_renameButton->setEnabled(hasSingleSelection);
    m_deleteButton->setEnabled(hasSingleSelection);
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
    const QList<QListWidgetItem*> selected = m_tagList->selectedItems();
    if (selected.size() != 1)
    {
        return;
    }

    const Tag::Id id = static_cast<Tag::Id>(selected.first()->data(kTagIdRole).toLongLong());
    const QString currentName = selected.first()->text();

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Tag"), tr("New name:"), QLineEdit::Normal,
                                                    currentName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == currentName)
    {
        return;
    }

    m_viewModel->renameTag(id, newName.trimmed());
}

void TagManagerDialog::onDeleteClicked()
{
    const QList<QListWidgetItem*> selected = m_tagList->selectedItems();
    if (selected.size() != 1)
    {
        return;
    }

    const Tag::Id id = static_cast<Tag::Id>(selected.first()->data(kTagIdRole).toLongLong());
    const QString name = selected.first()->text();

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Delete Tag"),
                               tr("Delete tag \"%1\"? It will be removed from every file and folder it is applied to.")
                                   .arg(name));
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    m_viewModel->deleteTag(id);
}

void TagManagerDialog::onOperationFailed(const QString& message)
{
    QMessageBox::warning(this, tr("Tag Manager"), message);
}
