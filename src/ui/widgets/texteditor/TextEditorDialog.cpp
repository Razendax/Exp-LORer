#include "TextEditorDialog.h"

#include <QVBoxLayout>

#include "TextEditorPanel.h"

TextEditorDialog::TextEditorDialog(const TextEditorOptions& options, QWidget* parent)
    : QDialog(parent)
    , m_panel(new TextEditorPanel(options, this))
{
    setWindowTitle(tr("Text Editor"));
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_panel);
}
