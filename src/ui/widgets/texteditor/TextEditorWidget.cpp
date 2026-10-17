#include "TextEditorWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QStringList>
#include <QTimer>

#include <algorithm>

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

void TextEditorWidget::deleteAtAllCursors(bool forward, bool wholeWord)
{
    QTextCursor primary = textCursor();
    primary.beginEditBlock();

    if (primary.hasSelection())
        primary.removeSelectedText();
    else if (wholeWord) {
        primary.movePosition(forward ? QTextCursor::NextWord : QTextCursor::PreviousWord,
                              QTextCursor::KeepAnchor);
        primary.removeSelectedText();
    } else if (forward)
        primary.deleteChar();
    else
        primary.deletePreviousChar();

    for (QTextCursor& cursor : m_secondaryCursors) {
        if (cursor.hasSelection())
            cursor.removeSelectedText();
        else if (wholeWord) {
            cursor.movePosition(forward ? QTextCursor::NextWord : QTextCursor::PreviousWord,
                                 QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        } else if (forward)
            cursor.deleteChar();
        else
            cursor.deletePreviousChar();
    }

    primary.endEditBlock();
    setTextCursor(primary);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
}

void TextEditorWidget::copyOrCutAtAllCursors(bool cut)
{
    QVector<QTextCursor*> cursors;
    QTextCursor primary = textCursor();
    cursors.append(&primary);
    for (QTextCursor& cursor : m_secondaryCursors)
        cursors.append(&cursor);

    std::sort(cursors.begin(), cursors.end(), [](QTextCursor* a, QTextCursor* b) {
        return a->selectionStart() < b->selectionStart();
    });

    bool anySelection = false;
    QStringList segments;
    for (QTextCursor* cursor : cursors) {
        if (cursor->hasSelection())
            anySelection = true;
        QString segment = cursor->selectedText();
        segment.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        segments.append(segment);
    }

    if (!anySelection)
        return;

    QApplication::clipboard()->setText(segments.join(QStringLiteral("\n")));

    if (!cut)
        return;

    primary.beginEditBlock();
    for (QTextCursor* cursor : cursors) {
        if (cursor->hasSelection())
            cursor->removeSelectedText();
    }
    primary.endEditBlock();
    setTextCursor(primary);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
}

void TextEditorWidget::pasteAtAllCursors()
{
    const QString clipboardText = QApplication::clipboard()->text();
    if (clipboardText.isEmpty())
        return;

    QVector<QTextCursor*> cursors;
    QTextCursor primary = textCursor();
    cursors.append(&primary);
    for (QTextCursor& cursor : m_secondaryCursors)
        cursors.append(&cursor);

    std::sort(cursors.begin(), cursors.end(), [](QTextCursor* a, QTextCursor* b) {
        return a->position() < b->position();
    });

    const QStringList lines = clipboardText.split(QLatin1Char('\n'));

    primary.beginEditBlock();
    if (cursors.size() > 1 && lines.size() == cursors.size()) {
        for (int i = 0; i < cursors.size(); ++i)
            cursors[i]->insertText(lines.at(i));
    } else {
        for (QTextCursor* cursor : cursors)
            cursor->insertText(clipboardText);
    }
    primary.endEditBlock();
    setTextCursor(primary);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
}

void TextEditorWidget::moveAllCursors(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode)
{
    QTextCursor primary = textCursor();
    primary.movePosition(operation, mode);
    setTextCursor(primary);

    for (QTextCursor& cursor : m_secondaryCursors)
        cursor.movePosition(operation, mode);

    mergeCollidingCursors();
    updateSecondarySelectionHighlight();
    viewport()->update();
}

bool TextEditorWidget::handleMultiCursorKeyPress(QKeyEvent* event)
{
    const QString text = event->text();
    const bool isPrintable = !text.isEmpty() && text.at(0).isPrint();
    const QTextCursor::MoveMode moveMode = (event->modifiers() & Qt::ShiftModifier)
        ? QTextCursor::KeepAnchor
        : QTextCursor::MoveAnchor;

    switch (event->key()) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        // A bare modifier key-press (no accompanying key yet) carries no text and matches no
        // other case below; it must not fall through to the "clear secondaries" default, or
        // secondaries vanish the instant the user presses Ctrl before Ctrl+Left/Right lands.
        return false;
    case Qt::Key_Backspace:
        deleteAtAllCursors(false, event->modifiers() & Qt::ControlModifier);
        return true;
    case Qt::Key_Delete:
        deleteAtAllCursors(true, event->modifiers() & Qt::ControlModifier);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertAtAllCursors(QStringLiteral("\n"));
        return true;
    case Qt::Key_Tab:
        insertAtAllCursors(QStringLiteral("\t"));
        return true;
    case Qt::Key_Left:
        moveAllCursors((event->modifiers() & Qt::ControlModifier) ? QTextCursor::PreviousWord
                                                                    : QTextCursor::Left,
                        moveMode);
        return true;
    case Qt::Key_Right:
        moveAllCursors((event->modifiers() & Qt::ControlModifier) ? QTextCursor::NextWord
                                                                    : QTextCursor::Right,
                        moveMode);
        return true;
    case Qt::Key_Up:
        moveAllCursors(QTextCursor::Up, moveMode);
        return true;
    case Qt::Key_Down:
        moveAllCursors(QTextCursor::Down, moveMode);
        return true;
    case Qt::Key_Home:
        moveAllCursors(QTextCursor::StartOfLine, moveMode);
        return true;
    case Qt::Key_End:
        moveAllCursors(QTextCursor::EndOfLine, moveMode);
        return true;
    case Qt::Key_C:
        if (event->modifiers() & Qt::ControlModifier) {
            copyOrCutAtAllCursors(false);
            return true;
        }
        break;
    case Qt::Key_X:
        if (event->modifiers() & Qt::ControlModifier) {
            copyOrCutAtAllCursors(true);
            return true;
        }
        break;
    case Qt::Key_V:
        if (event->modifiers() & Qt::ControlModifier) {
            pasteAtAllCursors();
            return true;
        }
        break;
    default:
        break;
    }

    if (isPrintable) {
        insertAtAllCursors(text);
        return true;
    }

    // Anything else (Ctrl+Home/End, PageUp/Down, Ctrl+Z, ...) collapses secondaries and falls
    // through to normal single-cursor handling.
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
