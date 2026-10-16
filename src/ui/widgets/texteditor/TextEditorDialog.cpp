#include "TextEditorDialog.h"

#include <QVBoxLayout>

#include "TextEditorWidget.h"

TextEditorDialog::TextEditorDialog(QWidget* parent)
    : QDialog(parent)
    , m_editor(new TextEditorWidget(this))
{
    setWindowTitle(tr("Text Editor"));
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);
}
