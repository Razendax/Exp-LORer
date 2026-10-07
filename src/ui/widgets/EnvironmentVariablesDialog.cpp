#include "EnvironmentVariablesDialog.h"

#include <algorithm>
#include <utility>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "EditEnvironmentVariableDialog.h"
#include "EnvironmentVariablesViewModel.h"

namespace
{
    constexpr int kNameColumn = 0;
    constexpr int kValueColumn = 1;
}

EnvironmentVariablesDialog::EnvironmentVariablesDialog(EnvironmentVariablesViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
    , m_workingCopy(viewModel->variables())
{
    setObjectName(QStringLiteral("environmentVariablesDialog"));
    setWindowTitle(tr("Environment Variables"));
    resize(560, 420);

    auto* mainLayout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("environmentVariablesTable"));
    m_table->setHorizontalHeaderLabels({ tr("Variable"), tr("Value") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_table, 1);

    auto* buttonLayout = new QHBoxLayout();
    auto* newButton = new QPushButton(tr("New"), this);
    newButton->setObjectName(QStringLiteral("environmentVariablesNewButton"));
    m_editButton = new QPushButton(tr("Edit"), this);
    m_editButton->setObjectName(QStringLiteral("environmentVariablesEditButton"));
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setObjectName(QStringLiteral("environmentVariablesDeleteButton"));
    buttonLayout->addWidget(newButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    auto* dialogButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(dialogButtonBox);

    connect(newButton, &QPushButton::clicked, this, &EnvironmentVariablesDialog::onNewClicked);
    connect(m_editButton, &QPushButton::clicked, this, &EnvironmentVariablesDialog::onEditClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &EnvironmentVariablesDialog::onDeleteClicked);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &EnvironmentVariablesDialog::onEditClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &EnvironmentVariablesDialog::onSelectionChanged);
    connect(dialogButtonBox, &QDialogButtonBox::accepted, this, &EnvironmentVariablesDialog::onAccept);
    connect(dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildTable();
}

void EnvironmentVariablesDialog::rebuildTable()
{
    m_table->setRowCount(static_cast<int>(m_workingCopy.size()));
    for (int row = 0; row < static_cast<int>(m_workingCopy.size()); ++row)
    {
        const EnvironmentVariable& variable = m_workingCopy[static_cast<std::size_t>(row)];
        m_table->setItem(row, kNameColumn, new QTableWidgetItem(variable.name));
        m_table->setItem(row, kValueColumn, new QTableWidgetItem(joinEnvironmentVariableValues(variable.values)));
    }
    onSelectionChanged();
}

void EnvironmentVariablesDialog::onSelectionChanged()
{
    const bool hasSelection = m_table->currentRow() >= 0;
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}

void EnvironmentVariablesDialog::onNewClicked()
{
    editVariableAt(-1);
}

void EnvironmentVariablesDialog::onEditClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
    {
        return;
    }
    editVariableAt(row);
}

void EnvironmentVariablesDialog::onDeleteClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
    {
        return;
    }
    m_workingCopy.erase(m_workingCopy.begin() + row);
    rebuildTable();
}

void EnvironmentVariablesDialog::editVariableAt(int rowIndex)
{
    EditEnvironmentVariableDialog dialog(this);
    if (rowIndex >= 0)
    {
        dialog.setVariable(m_workingCopy[static_cast<std::size_t>(rowIndex)]);
    }

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const EnvironmentVariable edited = dialog.variable();

    for (std::size_t i = 0; i < m_workingCopy.size(); ++i)
    {
        if (static_cast<int>(i) == rowIndex)
        {
            continue;
        }
        if (m_workingCopy[i].name.compare(edited.name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::warning(this, tr("Environment Variables"),
                                  tr("A variable named \"%1\" already exists.").arg(edited.name));
            return;
        }
    }

    if (rowIndex >= 0)
    {
        m_workingCopy[static_cast<std::size_t>(rowIndex)] = edited;
    }
    else
    {
        m_workingCopy.push_back(edited);
    }

    rebuildTable();
}

void EnvironmentVariablesDialog::onAccept()
{
    m_viewModel->save(std::move(m_workingCopy));
    accept();
}
