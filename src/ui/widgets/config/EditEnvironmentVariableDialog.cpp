#include "EditEnvironmentVariableDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

EditEnvironmentVariableDialog::EditEnvironmentVariableDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("editEnvironmentVariableDialog"));
    setWindowTitle(tr("Environment Variable"));
    resize(420, 360);

    auto* mainLayout = new QVBoxLayout(this);

    auto* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Name:"), this));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("editEnvironmentVariableNameEdit"));
    nameLayout->addWidget(m_nameEdit, 1);
    mainLayout->addLayout(nameLayout);

    auto* listLayout = new QHBoxLayout();

    m_valuesList = new QListWidget(this);
    m_valuesList->setObjectName(QStringLiteral("editEnvironmentVariableValuesList"));
    listLayout->addWidget(m_valuesList, 1);

    auto* buttonColumn = new QVBoxLayout();
    auto* newValueButton = new QPushButton(tr("New"), this);
    newValueButton->setObjectName(QStringLiteral("editEnvironmentVariableNewValueButton"));
    m_editValueButton = new QPushButton(tr("Edit"), this);
    m_editValueButton->setObjectName(QStringLiteral("editEnvironmentVariableEditValueButton"));
    m_deleteValueButton = new QPushButton(tr("Delete"), this);
    m_deleteValueButton->setObjectName(QStringLiteral("editEnvironmentVariableDeleteValueButton"));
    m_moveUpButton = new QPushButton(tr("Move Up"), this);
    m_moveUpButton->setObjectName(QStringLiteral("editEnvironmentVariableMoveUpButton"));
    m_moveDownButton = new QPushButton(tr("Move Down"), this);
    m_moveDownButton->setObjectName(QStringLiteral("editEnvironmentVariableMoveDownButton"));

    buttonColumn->addWidget(newValueButton);
    buttonColumn->addWidget(m_editValueButton);
    buttonColumn->addWidget(m_deleteValueButton);
    buttonColumn->addWidget(m_moveUpButton);
    buttonColumn->addWidget(m_moveDownButton);
    buttonColumn->addStretch();
    listLayout->addLayout(buttonColumn);

    mainLayout->addLayout(listLayout, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(newValueButton, &QPushButton::clicked, this, &EditEnvironmentVariableDialog::onNewValueClicked);
    connect(m_editValueButton, &QPushButton::clicked, this, &EditEnvironmentVariableDialog::onEditValueClicked);
    connect(m_deleteValueButton, &QPushButton::clicked, this, &EditEnvironmentVariableDialog::onDeleteValueClicked);
    connect(m_moveUpButton, &QPushButton::clicked, this, &EditEnvironmentVariableDialog::onMoveValueUpClicked);
    connect(m_moveDownButton, &QPushButton::clicked, this, &EditEnvironmentVariableDialog::onMoveValueDownClicked);
    connect(m_valuesList, &QListWidget::itemDoubleClicked, this, &EditEnvironmentVariableDialog::onEditValueClicked);
    connect(m_valuesList, &QListWidget::currentRowChanged, this, &EditEnvironmentVariableDialog::onSelectionChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &EditEnvironmentVariableDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onSelectionChanged();
}

void EditEnvironmentVariableDialog::setVariable(const EnvironmentVariable& variable)
{
    m_nameEdit->setText(variable.name);
    m_valuesList->clear();
    for (const QString& value : variable.values)
    {
        m_valuesList->addItem(value);
    }
    onSelectionChanged();
}

EnvironmentVariable EditEnvironmentVariableDialog::variable() const
{
    EnvironmentVariable result;
    result.name = m_nameEdit->text().trimmed();
    for (int row = 0; row < m_valuesList->count(); ++row)
    {
        result.values.append(m_valuesList->item(row)->text());
    }
    return result;
}

void EditEnvironmentVariableDialog::onAccept()
{
    if (m_nameEdit->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Environment Variable"), tr("Name must not be empty."));
        return;
    }
    accept();
}

void EditEnvironmentVariableDialog::onNewValueClicked()
{
    bool ok = false;
    const QString value = QInputDialog::getText(this, tr("New Value"), tr("Value:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || value.isEmpty())
    {
        return;
    }
    m_valuesList->addItem(value);
}

void EditEnvironmentVariableDialog::onEditValueClicked()
{
    QListWidgetItem* current = m_valuesList->currentItem();
    if (!current)
    {
        return;
    }

    bool ok = false;
    const QString value =
        QInputDialog::getText(this, tr("Edit Value"), tr("Value:"), QLineEdit::Normal, current->text(), &ok);
    if (!ok || value.isEmpty())
    {
        return;
    }
    current->setText(value);
}

void EditEnvironmentVariableDialog::onDeleteValueClicked()
{
    QListWidgetItem* current = m_valuesList->currentItem();
    if (!current)
    {
        return;
    }
    delete m_valuesList->takeItem(m_valuesList->row(current));
}

void EditEnvironmentVariableDialog::onMoveValueUpClicked()
{
    const int row = m_valuesList->currentRow();
    if (row <= 0)
    {
        return;
    }
    QListWidgetItem* item = m_valuesList->takeItem(row);
    m_valuesList->insertItem(row - 1, item);
    m_valuesList->setCurrentRow(row - 1);
}

void EditEnvironmentVariableDialog::onMoveValueDownClicked()
{
    const int row = m_valuesList->currentRow();
    if (row < 0 || row + 1 >= m_valuesList->count())
    {
        return;
    }
    QListWidgetItem* item = m_valuesList->takeItem(row);
    m_valuesList->insertItem(row + 1, item);
    m_valuesList->setCurrentRow(row + 1);
}

void EditEnvironmentVariableDialog::onSelectionChanged()
{
    const int row = m_valuesList->currentRow();
    const bool hasSelection = row >= 0;
    m_editValueButton->setEnabled(hasSelection);
    m_deleteValueButton->setEnabled(hasSelection);
    m_moveUpButton->setEnabled(hasSelection && row > 0);
    m_moveDownButton->setEnabled(hasSelection && row + 1 < m_valuesList->count());
}
