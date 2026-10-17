#include "TextEditorDialog.h"

#include <QKeySequence>
#include <QShortcut>
#include <QVBoxLayout>

#include "TextEditorFindBar.h"
#include "TextEditorWidget.h"

TextEditorDialog::TextEditorDialog(QWidget* parent)
    : QDialog(parent)
    , m_editor(new TextEditorWidget(this))
    , m_findBar(new TextEditorFindBar(m_editor))
{
    setWindowTitle(tr("Text Editor"));
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    // Find bar is a floating overlay anchored to m_editor's own top-right corner (see
    // TextEditorFindBar), not a widget in this dialog's layout.
    m_findBar->setVisible(false);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, m_findBar, &TextEditorFindBar::showAndFocus);
}
