#include "TextEditorDialog.h"

#include <QKeySequence>
#include <QShortcut>
#include <QVBoxLayout>

#include "TextEditorFindBar.h"
#include "TextEditorGotoLineBar.h"
#include "TextEditorWidget.h"

TextEditorDialog::TextEditorDialog(QWidget* parent)
    : QDialog(parent)
    , m_editor(new TextEditorWidget(this))
    , m_findBar(new TextEditorFindBar(m_editor))
    , m_gotoLineBar(new TextEditorGotoLineBar(m_editor))
{
    setWindowTitle(tr("Text Editor"));
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    // Both bars are floating overlays anchored to m_editor's own top-right corner (see
    // TextEditorFindBar / TextEditorGotoLineBar), not widgets in this dialog's layout.
    m_findBar->setVisible(false);
    m_gotoLineBar->setVisible(false);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    auto* gotoLineShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);

    // The two bars float over the same corner, so opening one must close the other. Handled here
    // (the one place that already owns and wires both) rather than inside either bar class, to
    // keep the two bar classes mutually unaware of each other.
    connect(findShortcut, &QShortcut::activated, this, [this] {
        m_gotoLineBar->hideAndClear();
        m_findBar->showAndFocus();
    });
    connect(gotoLineShortcut, &QShortcut::activated, this, [this] {
        m_findBar->hideAndClear();
        m_gotoLineBar->showAndFocus();
    });
}
