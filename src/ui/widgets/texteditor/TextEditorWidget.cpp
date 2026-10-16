#include "TextEditorWidget.h"

#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

TextEditorWidget::TextEditorWidget(QWidget* parent)
    : QPlainTextEdit(parent)
{
    m_secondaryCaretBlinkTimer = new QTimer(this);
    connect(m_secondaryCaretBlinkTimer, &QTimer::timeout, this, [this] {
        m_secondaryCaretVisible = !m_secondaryCaretVisible;
        viewport()->update();
    });
}

bool TextEditorWidget::hasSecondaryCursors() const
{
    return !m_secondaryCursors.isEmpty();
}

void TextEditorWidget::addSecondaryCursorAt(const QTextCursor& cursor)
{
    if (cursor.position() == textCursor().position())
        return;

    for (const QTextCursor& existing : m_secondaryCursors) {
        if (existing.position() == cursor.position())
            return;
    }

    m_secondaryCursors.append(cursor);
    updateSecondarySelectionHighlight();
    viewport()->update();
}

void TextEditorWidget::addCursorOnAdjacentLine(bool below)
{
    // The outermost cursor in the requested direction is the one that would need to move
    // furthest to get there, i.e. has no other cursor beyond it on that side.
    QTextCursor outermost = textCursor();
    for (const QTextCursor& existing : m_secondaryCursors) {
        if (below ? (existing.blockNumber() > outermost.blockNumber())
                  : (existing.blockNumber() < outermost.blockNumber())) {
            outermost = existing;
        }
    }

    QTextCursor newCursor = outermost;
    newCursor.movePosition(below ? QTextCursor::Down : QTextCursor::Up);
    addSecondaryCursorAt(newCursor);
}

void TextEditorWidget::clearSecondaryCursors()
{
    if (m_secondaryCursors.isEmpty())
        return;

    m_secondaryCursors.clear();
    updateSecondarySelectionHighlight();
    viewport()->update();
}

void TextEditorWidget::mergeCollidingCursors()
{
    const int primaryPosition = textCursor().position();

    QVector<QTextCursor> merged;
    merged.reserve(m_secondaryCursors.size());
    for (const QTextCursor& candidate : m_secondaryCursors) {
        if (candidate.position() == primaryPosition)
            continue;

        bool duplicate = false;
        for (const QTextCursor& already : merged) {
            if (already.position() == candidate.position()) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            merged.append(candidate);
    }

    m_secondaryCursors = merged;
}

void TextEditorWidget::updateSecondarySelectionHighlight()
{
    QList<QTextEdit::ExtraSelection> selections;
    for (const QTextCursor& cursor : m_secondaryCursors) {
        if (!cursor.hasSelection())
            continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setBackground(palette().color(QPalette::Highlight));
        selections.append(selection);
    }
    setExtraSelections(selections);
}

void TextEditorWidget::insertAtAllCursors(const QString& text)
{
    QTextCursor primary = textCursor();
    primary.beginEditBlock();
    primary.insertText(text);

    for (QTextCursor& cursor : m_secondaryCursors)
        cursor.insertText(text);

    primary.endEditBlock();
    setTextCursor(primary);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
}

void TextEditorWidget::deleteAtAllCursors(bool forward)
{
    QTextCursor primary = textCursor();
    primary.beginEditBlock();

    if (primary.hasSelection())
        primary.removeSelectedText();
    else if (forward)
        primary.deleteChar();
    else
        primary.deletePreviousChar();

    for (QTextCursor& cursor : m_secondaryCursors) {
        if (cursor.hasSelection())
            cursor.removeSelectedText();
        else if (forward)
            cursor.deleteChar();
        else
            cursor.deletePreviousChar();
    }

    primary.endEditBlock();
    setTextCursor(primary);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
}

void TextEditorWidget::moveAllCursors(QTextCursor::MoveOperation operation)
{
    QTextCursor primary = textCursor();
    primary.movePosition(operation, QTextCursor::MoveAnchor);
    setTextCursor(primary);

    for (QTextCursor& cursor : m_secondaryCursors)
        cursor.movePosition(operation, QTextCursor::MoveAnchor);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
    viewport()->update();
}

bool TextEditorWidget::handleMultiCursorKeyPress(QKeyEvent* event)
{
    const QString text = event->text();
    const bool isPrintable = !text.isEmpty() && text.at(0).isPrint();

    switch (event->key()) {
    case Qt::Key_Backspace:
        deleteAtAllCursors(false);
        return true;
    case Qt::Key_Delete:
        deleteAtAllCursors(true);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertAtAllCursors(QStringLiteral("\n"));
        return true;
    case Qt::Key_Tab:
        insertAtAllCursors(QStringLiteral("\t"));
        return true;
    case Qt::Key_Left:
        moveAllCursors(QTextCursor::Left);
        return true;
    case Qt::Key_Right:
        moveAllCursors(QTextCursor::Right);
        return true;
    case Qt::Key_Up:
        moveAllCursors(QTextCursor::Up);
        return true;
    case Qt::Key_Down:
        moveAllCursors(QTextCursor::Down);
        return true;
    default:
        break;
    }

    if (isPrintable) {
        insertAtAllCursors(text);
        return true;
    }

    // Anything else (Home/End/PageUp/Down, Ctrl+Z, ...) collapses secondaries and falls through
    // to normal single-cursor handling.
    clearSecondaryCursors();
    return false;
}

void TextEditorWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && hasSecondaryCursors()) {
        clearSecondaryCursors();
        return;
    }

    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)) {
            addCursorOnAdjacentLine(event->key() == Qt::Key_Down);
            return;
        }
    }

    if (hasSecondaryCursors() && handleMultiCursorKeyPress(event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void TextEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        const QTextCursor cursor = cursorForPosition(event->pos());
        addSecondaryCursorAt(cursor);
        m_extendingSecondarySelection = true;
        return;
    }

    if (hasSecondaryCursors())
        clearSecondaryCursors();

    QPlainTextEdit::mousePressEvent(event);
}

void TextEditorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_extendingSecondarySelection && !m_secondaryCursors.isEmpty()) {
        QTextCursor& extending = m_secondaryCursors.last();
        const QTextCursor moved = cursorForPosition(event->pos());
        extending.setPosition(moved.position(), QTextCursor::KeepAnchor);
        updateSecondarySelectionHighlight();
        viewport()->update();
        return;
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void TextEditorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_extendingSecondarySelection) {
        m_extendingSecondarySelection = false;
        return;
    }

    QPlainTextEdit::mouseReleaseEvent(event);
}

void TextEditorWidget::paintEvent(QPaintEvent* event)
{
    QPlainTextEdit::paintEvent(event);

    if (!m_secondaryCaretVisible || !hasFocus() || m_secondaryCursors.isEmpty())
        return;

    QPainter painter(viewport());
    for (const QTextCursor& cursor : m_secondaryCursors)
        painter.fillRect(cursorRect(cursor), palette().color(QPalette::Text));
}

void TextEditorWidget::focusInEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusInEvent(event);

    const int flashTime = QApplication::cursorFlashTime();
    if (flashTime > 0) {
        m_secondaryCaretVisible = true;
        m_secondaryCaretBlinkTimer->start(flashTime / 2);
    }
}

void TextEditorWidget::focusOutEvent(QFocusEvent* event)
{
    m_secondaryCaretBlinkTimer->stop();
    m_secondaryCaretVisible = true;

    QPlainTextEdit::focusOutEvent(event);
}
