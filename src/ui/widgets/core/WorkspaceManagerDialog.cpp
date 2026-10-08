#include "WorkspaceManagerDialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "WorkspaceInstanceProbe.h"
#include "WorkspaceStore.h"

namespace
{
    constexpr int kIdRole = Qt::UserRole;
}

WorkspaceManagerDialog::WorkspaceManagerDialog(WorkspaceStore& workspaceStore, const WorkspaceLaunchContext& launchContext,
                                                 QWidget* parent)
    : QDialog(parent)
    , m_workspaceStore(workspaceStore)
    , m_launchContext(launchContext)
{
    setObjectName(QStringLiteral("workspaceManagerDialog"));
    setWindowTitle(tr("Workspace Management"));
    resize(420, 360);

    auto* mainLayout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("workspaceManagerList"));
    mainLayout->addWidget(m_list, 1);

    auto* buttonLayout = new QHBoxLayout();

    m_renameButton = new QPushButton(tr("Rename..."), this);
    m_renameButton->setObjectName(QStringLiteral("workspaceManagerRenameButton"));
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setObjectName(QStringLiteral("workspaceManagerDeleteButton"));
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("workspaceManagerCloseButton"));

    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) { onSelectionChanged(); });
    connect(m_renameButton, &QPushButton::clicked, this, &WorkspaceManagerDialog::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &WorkspaceManagerDialog::onDeleteClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    rebuildList();
}

void WorkspaceManagerDialog::rebuildList()
{
    m_list->clear();

    for (const WorkspaceEntry& entry : m_workspaceStore.listWorkspaces())
    {
        auto* item = new QListWidgetItem(entry.name, m_list);
        item->setData(kIdRole, entry.id);
        if (entry.id == m_launchContext.workspaceId)
        {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
    }

    onSelectionChanged();
}

void WorkspaceManagerDialog::onSelectionChanged()
{
    const bool hasSelection = m_list->currentItem() != nullptr;
    m_renameButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}

void WorkspaceManagerDialog::onRenameClicked()
{
    QListWidgetItem* current = m_list->currentItem();
    if (!current)
    {
        return;
    }

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("Rename Workspace"), tr("New name:"), QLineEdit::Normal, current->text(), &ok);
    if (!ok || newName.trimmed().isEmpty())
    {
        return;
    }

    m_workspaceStore.renameWorkspace(current->data(kIdRole).toString(), newName.trimmed());
    rebuildList();
}

void WorkspaceManagerDialog::onDeleteClicked()
{
    QListWidgetItem* current = m_list->currentItem();
    if (!current)
    {
        return;
    }

    const QString id = current->data(kIdRole).toString();

    if (WorkspaceInstanceProbe::isOwned(m_launchContext.appDataDir, id))
    {
        QMessageBox::information(this, tr("Workspace Management"),
                                  tr("\"%1\" is currently open in a window. Close it before deleting.").arg(current->text()));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Delete Workspace"), tr("Delete the workspace \"%1\"? This cannot be undone.").arg(current->text()));
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    m_workspaceStore.deleteWorkspace(id);
    rebuildList();
}
