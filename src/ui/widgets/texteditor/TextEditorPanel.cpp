#include "TextEditorPanel.h"

#include <QKeySequence>
#include <QShortcut>
#include <QVBoxLayout>

#include "TextEditorFindBar.h"
#include "TextEditorGotoLineBar.h"
#include "TextEditorWidget.h"

TextEditorPanel::TextEditorPanel(const TextEditorOptions& options, QWidget* parent,
                                  SyntaxHighlightEngine* highlightEngine,
                                  HighlightThemeViewModel* highlightThemeViewModel)
    : QWidget(parent)
    , m_editor(new TextEditorWidget(options, this, highlightEngine, highlightThemeViewModel))
    , m_findBar(options.findEnabled ? new TextEditorFindBar(m_editor, options.multicursorEnabled, options.readOnly)
                                     : nullptr)
    , m_gotoLineBar(options.goToLineEnabled ? new TextEditorGotoLineBar(m_editor) : nullptr)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    // Both bars are floating overlays anchored to m_editor's own top-right corner (see
    // TextEditorFindBar / TextEditorGotoLineBar), not widgets in this panel's layout.
    if (m_findBar)
        m_findBar->setVisible(false);
    if (m_gotoLineBar)
        m_gotoLineBar->setVisible(false);

    // The two bars float over the same corner, so opening one must close the other. Handled here
    // (the one place that already owns and wires both) rather than inside either bar class, to
    // keep the two bar classes mutually unaware of each other. Each lambda null-checks the other
    // bar before calling hideAndClear() on it, since it may not exist when disabled.
    if (m_findBar) {
        auto* findShortcut = new QShortcut(QKeySequence::Find, this);
        findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(findShortcut, &QShortcut::activated, this, [this] {
            if (m_gotoLineBar)
                m_gotoLineBar->hideAndClear();
            m_findBar->showAndFocus();
        });
    }
    if (m_gotoLineBar) {
        auto* gotoLineShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
        gotoLineShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(gotoLineShortcut, &QShortcut::activated, this, [this] {
            if (m_findBar)
                m_findBar->hideAndClear();
            m_gotoLineBar->showAndFocus();
        });
    }
}
