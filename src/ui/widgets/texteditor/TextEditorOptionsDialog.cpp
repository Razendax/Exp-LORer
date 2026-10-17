#include "TextEditorOptionsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

TextEditorOptionsDialog::TextEditorOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Text Editor Options"));

    m_findCheckBox = new QCheckBox(tr("Find"), this);
    m_findCheckBox->setChecked(true);

    m_goToLineCheckBox = new QCheckBox(tr("Go to Line"), this);
    m_goToLineCheckBox->setChecked(true);

    m_lineNumbersCheckBox = new QCheckBox(tr("Line Numbers"), this);
    m_lineNumbersCheckBox->setChecked(true);

    m_multicursorCheckBox = new QCheckBox(tr("Multicursor"), this);
    m_multicursorCheckBox->setChecked(true);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Open Editor"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_findCheckBox);
    layout->addWidget(m_goToLineCheckBox);
    layout->addWidget(m_lineNumbersCheckBox);
    layout->addWidget(m_multicursorCheckBox);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TextEditorOptions TextEditorOptionsDialog::options() const
{
    TextEditorOptions result;
    result.findEnabled = m_findCheckBox->isChecked();
    result.goToLineEnabled = m_goToLineCheckBox->isChecked();
    result.lineNumbersEnabled = m_lineNumbersCheckBox->isChecked();
    result.multicursorEnabled = m_multicursorCheckBox->isChecked();
    return result;
}
