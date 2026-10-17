#include "TextEditorGotoLineBar.h"

#include "TextEditorWidget.h"

#include <QAbstractScrollArea>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRect>
#include <QTextCursor>

TextEditorGotoLineBar::TextEditorGotoLineBar(TextEditorWidget* editor)
    : QWidget(editor)
    , m_editor(editor)
{
    // Keep the bar's own sizeHint compact -- it floats over the editor rather than being
    // stretched by an enclosing layout (see repositionOverEditor).
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    // Floating directly over editor text needs its own opaque background and a border to read
    // as a distinct panel, VS Code-style (same styling as TextEditorFindBar).
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "TextEditorGotoLineBar { background: palette(window); border: 1px solid palette(mid); border-radius: 4px; }"));

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setFixedWidth(80);
    m_lineEdit->installEventFilter(this);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addWidget(m_lineEdit);

    connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString&) { jumpToEnteredLine(false); });

    // Reposition whenever the editor (this widget's parent) is resized, so the overlay stays
    // anchored to its top-right corner.
    m_editor->installEventFilter(this);
    repositionOverEditor();
}

bool TextEditorGotoLineBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor && event->type() == QEvent::Resize) {
        repositionOverEditor();
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_lineEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideAndClear();
            return true;
        }
        // Intercepted here rather than via QLineEdit::returnPressed: QLineEdit's own
        // keyPressEvent ignores Return/Enter after emitting that signal (so dialogs can route
        // Enter to a default button), which lets the event bubble up the parent chain to
        // m_editor and insert a newline at the line we just jumped to. Consuming it here (like
        // Escape above) stops that propagation entirely.
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            jumpToEnteredLine(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TextEditorGotoLineBar::showAndFocus()
{
    const int lastLine = qMax(1, m_editor->blockCount());
    delete m_lineEdit->validator();
    m_lineEdit->setValidator(new QIntValidator(1, lastLine, m_lineEdit));
    m_lineEdit->setPlaceholderText(tr("Line (1-%1)").arg(lastLine));
    m_lineEdit->setText(QString::number(m_editor->textCursor().blockNumber() + 1));

    setVisible(true);
    repositionOverEditor();
    raise();

    m_lineEdit->setFocus();
    m_lineEdit->selectAll();
}

void TextEditorGotoLineBar::repositionOverEditor()
{
    adjustSize();

    const QRect viewportRect = m_editor->viewport()->geometry();
    constexpr int margin = 6;
    const int x = qMax(viewportRect.left(), viewportRect.right() - width() - margin);
    const int y = viewportRect.top() + margin;
    move(x, y);
}

void TextEditorGotoLineBar::hideAndClear()
{
    setVisible(false);
    m_editor->setFocus();
}

void TextEditorGotoLineBar::jumpToEnteredLine(bool closeAfter)
{
    bool ok = false;
    const int line = m_lineEdit->text().toInt(&ok);
    if (ok && line >= 1)
        m_editor->goToLine(line);

    if (closeAfter)
        hideAndClear();
}
