#include "TerminalWidget.h"

#include <algorithm>

#include <QClipboard>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStringList>
#include <QWheelEvent>

#include "WindowsConPtyProcess.h"

namespace
{
    const QColor kDefaultForeground(0xE5, 0xE5, 0xE5);
    const QColor kDefaultBackground(0x0C, 0x0C, 0x0C);
    constexpr int kScrollLinesPerNotch = 3;

    // Modifier state a kShortcutTable entry requires, tested against (Ctrl|Shift) of the event --
    // every shortcut below needs an exact match on just those two (Alt/Meta are irrelevant to all
    // of them), so this covers the three combinations actually used.
    enum class ShortcutMods
    {
        CtrlShift,
        ShiftOnly,
        CtrlOnly,
    };

    struct ShortcutEntry
    {
        ShortcutMods mods;
        Qt::Key key;
        void (TerminalWidget::*handler)(QKeyEvent*);
    };

    // Plain key -> VT escape sequence forwarded to the child process, once no shortcut above
    // matched. Key_Tab and the default (printable text) case need extra logic and stay out of this
    // table -- see keyPressEvent().
    const QHash<int, QByteArray> kKeyEscapeSequences = {
        { Qt::Key_Return, QByteArrayLiteral("\r") },
        { Qt::Key_Enter, QByteArrayLiteral("\r") },
        { Qt::Key_Backspace, QByteArrayLiteral("\x08") },
        { Qt::Key_Escape, QByteArrayLiteral("\x1b") },
        { Qt::Key_Up, QByteArrayLiteral("\x1b[A") },
        { Qt::Key_Down, QByteArrayLiteral("\x1b[B") },
        { Qt::Key_Right, QByteArrayLiteral("\x1b[C") },
        { Qt::Key_Left, QByteArrayLiteral("\x1b[D") },
        { Qt::Key_Home, QByteArrayLiteral("\x1b[H") },
        { Qt::Key_End, QByteArrayLiteral("\x1b[F") },
        { Qt::Key_PageUp, QByteArrayLiteral("\x1b[5~") },
        { Qt::Key_PageDown, QByteArrayLiteral("\x1b[6~") },
        { Qt::Key_Delete, QByteArrayLiteral("\x1b[3~") },
        { Qt::Key_Insert, QByteArrayLiteral("\x1b[2~") },
    };
}

TerminalWidget::TerminalWidget(TerminalShellType shellType, QWidget* parent)
    : QWidget(parent)
    , m_shellType(shellType)
    , m_buffer(new AnsiTerminalBuffer(this))
    , m_process(new WindowsConPtyProcess(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_font.setFamily(QStringLiteral("Consolas"));
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPointSize(10);

    connect(m_buffer, &AnsiTerminalBuffer::contentChanged, this, [this]() {
        m_scrollOffset = 0;
        update();
    });
    connect(m_process, &WindowsConPtyProcess::dataReceived, this,
            [this](const QByteArray& bytes) { m_buffer->feed(bytes); });
    connect(m_process, &WindowsConPtyProcess::processExited, this, [this](int exitCode) {
        m_exitMessage = tr("Process exited (code %1) — click to restart").arg(exitCode);
        update();
    });
}

void TerminalWidget::start(const std::filesystem::path& workingDirectory)
{
    recomputeGridSize();
    m_exitMessage.clear();

    const auto result = m_process->start(workingDirectory, m_currentColumns, m_currentRows, m_shellType);
    if (!result)
    {
        m_exitMessage = QString::fromStdString(result.error().message) + tr(" — click to restart");
    }

    update();
}

int TerminalWidget::logicalRowCount() const
{
    return m_buffer->scrollbackLineCount() + m_buffer->rows();
}

const AnsiTerminalBuffer::Cell& TerminalWidget::cellAtLogicalRow(int logicalRow, int column) const
{
    const int scrollbackCount = m_buffer->scrollbackLineCount();
    if (logicalRow < scrollbackCount)
    {
        return m_buffer->scrollbackLine(logicalRow)[static_cast<std::size_t>(column)];
    }
    return m_buffer->cellAt(logicalRow - scrollbackCount, column);
}

int TerminalWidget::promptBoundaryColumn(int logicalRow) const
{
    if (logicalRow != m_buffer->scrollbackLineCount() + m_buffer->cursorRow())
    {
        return 0;
    }

    const int lastColumn = m_buffer->columns() - 1;
    for (int c = 0; c <= lastColumn; ++c)
    {
        if (cellAtLogicalRow(logicalRow, c).character == QLatin1Char('>'))
        {
            return c + 1;
        }
    }
    return 0;
}

int TerminalWidget::typedTextEndColumn(int logicalRow) const
{
    if (logicalRow != m_buffer->scrollbackLineCount() + m_buffer->cursorRow())
    {
        return m_buffer->columns() - 1;
    }

    const int promptEnd = promptBoundaryColumn(logicalRow);
    for (int c = m_buffer->columns() - 1; c >= promptEnd; --c)
    {
        if (cellAtLogicalRow(logicalRow, c).character != QLatin1Char(' '))
        {
            return c;
        }
    }
    return promptEnd - 1;
}

int TerminalWidget::lastNonSpaceColumn(int logicalRow) const
{
    for (int c = m_buffer->columns() - 1; c >= 0; --c)
    {
        if (cellAtLogicalRow(logicalRow, c).character != QLatin1Char(' '))
        {
            return c;
        }
    }
    return -1;
}

int TerminalWidget::forwardSelectionBoundary(int logicalRow) const
{
    int boundary = typedTextEndColumn(logicalRow);
    if (m_selectionAnchor && m_selectionAnchor->logicalRow == logicalRow)
    {
        boundary = std::max(boundary, m_selectionAnchor->column);
    }
    return boundary;
}

bool TerminalWidget::hasSelection() const
{
    return m_selectionAnchor.has_value();
}

void TerminalWidget::clearSelection()
{
    if (!m_selectionAnchor)
    {
        return;
    }
    m_selectionAnchor.reset();
    update();
}

void TerminalWidget::extendSelection(int deltaLogicalRow, int deltaColumn)
{
    if (!m_selectionAnchor)
    {
        m_selectionAnchor = SelectionPoint{ m_buffer->scrollbackLineCount() + m_buffer->cursorRow(),
                                             m_buffer->cursorColumn() };
        m_selectionActive = *m_selectionAnchor;
    }

    int row = m_selectionActive.logicalRow;
    int column = m_selectionActive.column + deltaColumn;

    if (deltaColumn < 0)
    {
        const int boundary = promptBoundaryColumn(row);
        if (column < boundary)
        {
            m_selectionActive = SelectionPoint{ row, boundary };
            update();
            return;
        }
    }
    else if (deltaColumn > 0)
    {
        const int boundary = forwardSelectionBoundary(row);
        if (column > boundary)
        {
            m_selectionActive = SelectionPoint{ row, boundary };
            update();
            return;
        }
    }

    if (column < 0)
    {
        --row;
        column = m_buffer->columns() - 1;
    }
    else if (column >= m_buffer->columns())
    {
        ++row;
        column = 0;
    }
    row += deltaLogicalRow;

    row = std::clamp(row, 0, logicalRowCount() - 1);
    column = std::clamp(column, 0, m_buffer->columns() - 1);

    m_selectionActive = SelectionPoint{ row, column };
    update();
}

void TerminalWidget::extendSelectionByWord(int direction)
{
    if (!m_selectionAnchor)
    {
        m_selectionAnchor = SelectionPoint{ m_buffer->scrollbackLineCount() + m_buffer->cursorRow(),
                                             m_buffer->cursorColumn() };
        m_selectionActive = *m_selectionAnchor;
    }

    const int row = m_selectionActive.logicalRow;
    int column = m_selectionActive.column;

    if (direction < 0)
    {
        // On the live input row, don't let word-wise selection walk back past the shell prompt
        // into the current directory text -- see promptBoundaryColumn().
        const int boundary = promptBoundaryColumn(row);

        // Mirrors the console's own word-left: skip the whitespace run immediately before the
        // cursor, then skip the word run before that, landing on the start of the previous word.
        // Both phases run in the same keypress, same as the VT sequence (CSI 1;5D) we forward to
        // the shell -- a single-phase skip would land half a word-boundary short of the shell's
        // own cursor and the two would drift apart over repeated presses.
        while (column > boundary && !isWordChar(cellAtLogicalRow(row, column - 1).character))
        {
            --column;
        }
        while (column > boundary && isWordChar(cellAtLogicalRow(row, column - 1).character))
        {
            --column;
        }
    }
    else
    {
        // On the live input row, don't let word-wise selection walk forward past the typed command
        // text into the untyped blank cells that fill out the rest of the row -- see
        // forwardSelectionBoundary().
        const int boundary = forwardSelectionBoundary(row);

        // Mirrors the console's own word-right: skip the rest of the current word run, then skip
        // the whitespace run that follows, landing on the start of the next word. See the matching
        // comment in the direction < 0 branch above for why both phases must run in one keypress.
        while (column < boundary && isWordChar(cellAtLogicalRow(row, column).character))
        {
            ++column;
        }
        while (column < boundary && !isWordChar(cellAtLogicalRow(row, column).character))
        {
            ++column;
        }
    }

    m_selectionActive = SelectionPoint{ row, column };
    update();
}

void TerminalWidget::selectAll()
{
    // Stop at the live row's typed content instead of the full grid: logicalRowCount() - 1 is the
    // bottom of the grid, which is usually several blank filler rows below the prompt, and
    // columns() - 1 is the right edge of the grid, which is blank padding past whatever was typed
    // or printed.
    const int lastRow = m_buffer->scrollbackLineCount() + m_buffer->cursorRow();
    m_selectionAnchor = SelectionPoint{ 0, 0 };
    m_selectionActive = SelectionPoint{ lastRow, typedTextEndColumn(lastRow) };
    update();
}

QString TerminalWidget::selectedText() const
{
    if (!m_selectionAnchor)
    {
        return QString();
    }

    SelectionPoint from = *m_selectionAnchor;
    SelectionPoint to = m_selectionActive;
    if (from.logicalRow > to.logicalRow || (from.logicalRow == to.logicalRow && from.column > to.column))
    {
        std::swap(from, to);
    }

    QStringList lines;
    for (int row = from.logicalRow; row <= to.logicalRow; ++row)
    {
        const int startColumn = (row == from.logicalRow) ? from.column : 0;
        const int endColumn = (row == to.logicalRow) ? to.column : m_buffer->columns() - 1;

        QString line;
        for (int column = startColumn; column <= endColumn; ++column)
        {
            line.append(cellAtLogicalRow(row, column).character);
        }
        while (line.endsWith(QLatin1Char(' ')))
        {
            line.chop(1);
        }
        lines.append(line);
    }

    return lines.join(QLatin1Char('\n'));
}

bool TerminalWidget::isWordChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

void TerminalWidget::recomputeGridSize()
{
    if (width() <= 0 || height() <= 0)
    {
        return;
    }

    const QFontMetrics metrics(m_font);
    const int charWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int charHeight = std::max(1, metrics.height());

    const int columns = std::max(1, width() / charWidth);
    const int rows = std::max(1, height() / charHeight);

    if (columns == m_currentColumns && rows == m_currentRows)
    {
        return;
    }

    m_currentColumns = columns;
    m_currentRows = rows;
    m_buffer->resize(columns, rows);
    m_process->resize(columns, rows);
}

void TerminalWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), kDefaultBackground);
    painter.setFont(m_font);

    if (!m_exitMessage.isEmpty())
    {
        painter.setPen(kDefaultForeground);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_exitMessage);
        return;
    }

    const QFontMetrics metrics(m_font);
    const int charWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int charHeight = std::max(1, metrics.height());
    const int ascent = metrics.ascent();
    const int scrollbackCount = m_buffer->scrollbackLineCount();

    SelectionPoint selectionFrom{};
    SelectionPoint selectionTo{};
    if (m_selectionAnchor)
    {
        selectionFrom = *m_selectionAnchor;
        selectionTo = m_selectionActive;
        if (selectionFrom.logicalRow > selectionTo.logicalRow
            || (selectionFrom.logicalRow == selectionTo.logicalRow && selectionFrom.column > selectionTo.column))
        {
            std::swap(selectionFrom, selectionTo);
        }
    }

    for (int visibleRow = 0; visibleRow < m_buffer->rows(); ++visibleRow)
    {
        const int logicalRow = scrollbackCount - m_scrollOffset + visibleRow;
        if (logicalRow < 0 || logicalRow >= scrollbackCount + m_buffer->rows())
        {
            continue;
        }

        const int y = visibleRow * charHeight;

        // Highlight range for this row, clamped to its own displayed text -- rows fully spanned
        // by the selection (logicalRow < selectionTo.logicalRow) use their own lastNonSpaceColumn()
        // as the right edge instead of columns() - 1, so multi-row selection (currently only reached
        // via selectAll()) doesn't paint over the blank padding past each line's actual content, and
        // a fully blank row highlights nothing (min > max) rather than its whole width.
        int selectionColMin = -1;
        int selectionColMax = -1;
        if (m_selectionAnchor && logicalRow >= selectionFrom.logicalRow && logicalRow <= selectionTo.logicalRow)
        {
            selectionColMin = (logicalRow == selectionFrom.logicalRow) ? selectionFrom.column : 0;
            selectionColMax = (logicalRow == selectionTo.logicalRow) ? selectionTo.column : lastNonSpaceColumn(logicalRow);
        }

        for (int col = 0; col < m_buffer->columns(); ++col)
        {
            const AnsiTerminalBuffer::Cell& cell = cellAtLogicalRow(logicalRow, col);
            const int x = col * charWidth;

            QColor foreground = cell.foreground.isValid() ? cell.foreground : kDefaultForeground;
            QColor background = cell.background.isValid() ? cell.background : kDefaultBackground;
            if (cell.inverse)
            {
                std::swap(foreground, background);
            }

            if (background != kDefaultBackground)
            {
                painter.fillRect(QRect(x, y, charWidth, charHeight), background);
            }

            const bool selected = col >= selectionColMin && col <= selectionColMax;
            if (selected)
            {
                painter.fillRect(QRect(x, y, charWidth, charHeight), QColor(100, 149, 237, 110));
            }

            if (cell.character != QLatin1Char(' '))
            {
                QFont cellFont = m_font;
                cellFont.setBold(cell.bold);
                cellFont.setUnderline(cell.underline);
                painter.setFont(cellFont);
                painter.setPen(foreground);
                painter.drawText(x, y + ascent, QString(cell.character));
            }
        }
    }

    if (m_scrollOffset == 0 && m_buffer->cursorVisible() && hasFocus())
    {
        const int x = m_buffer->cursorColumn() * charWidth;
        const int y = m_buffer->cursorRow() * charHeight;
        painter.fillRect(QRect(x, y, charWidth, charHeight), QColor(255, 255, 255, 120));
    }
}

void TerminalWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recomputeGridSize();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_exitMessage.isEmpty())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    // A bare modifier press (e.g. Ctrl going down before Shift, on the way to Ctrl+Shift+Left) is
    // its own keyPressEvent with event->key() == Qt::Key_Control/Key_Shift/etc. It matches none of
    // the shortcut checks below and would otherwise fall through to the catch-all clearSelection(),
    // wiping any selection before the real shortcut key is even pressed.
    switch (event->key())
    {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            event->accept();
            return;
        default:
            break;
    }

    const auto mods = event->modifiers();
    const bool ctrlOnly = (mods & (Qt::ControlModifier | Qt::ShiftModifier)) == Qt::ControlModifier;
    const bool ctrlShift = (mods & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier);
    const bool shiftOnly = (mods & (Qt::ControlModifier | Qt::ShiftModifier)) == Qt::ShiftModifier;
    const int key = event->key();

    // Modifier-combo + key shortcuts, checked in order before the plain-key forwarding below.
    // Ctrl+Shift+A (not plain Ctrl+A) is deliberate -- Ctrl+A is SOH (0x01), universally bound by
    // readline-based programs (bash, Python's REPL, vim/nano, an SSH session) to "move cursor to
    // start of line", and must keep falling through to the generic Ctrl+[A-Z] forwarding further
    // below (not part of this table -- it's a key *range*, not a fixed list). Same collision-
    // avoidance reasoning as Copy/Paste living on Ctrl+Shift+C/V instead of Ctrl+C/V. A function-
    // local static (rather than file-scope) because it takes the address of private member
    // functions, which is only accessible from within a TerminalWidget member function.
    static const ShortcutEntry kShortcutTable[] = {
        { ShortcutMods::CtrlShift, Qt::Key_Left, &TerminalWidget::handleWordSelectShortcut },
        { ShortcutMods::CtrlShift, Qt::Key_Right, &TerminalWidget::handleWordSelectShortcut },
        { ShortcutMods::CtrlShift, Qt::Key_C, &TerminalWidget::handleCopyShortcut },
        { ShortcutMods::CtrlShift, Qt::Key_V, &TerminalWidget::handlePasteShortcut },
        { ShortcutMods::ShiftOnly, Qt::Key_Left, &TerminalWidget::handleShiftSelectShortcut },
        { ShortcutMods::ShiftOnly, Qt::Key_Right, &TerminalWidget::handleShiftSelectShortcut },
        { ShortcutMods::CtrlShift, Qt::Key_A, &TerminalWidget::handleSelectAllShortcut },
        { ShortcutMods::CtrlOnly, Qt::Key_Left, &TerminalWidget::handleCtrlArrowWordNavShortcut },
        { ShortcutMods::CtrlOnly, Qt::Key_Right, &TerminalWidget::handleCtrlArrowWordNavShortcut },
    };

    for (const ShortcutEntry& entry : kShortcutTable)
    {
        const bool modsMatch = (entry.mods == ShortcutMods::CtrlShift && ctrlShift) ||
                                (entry.mods == ShortcutMods::ShiftOnly && shiftOnly) ||
                                (entry.mods == ShortcutMods::CtrlOnly && ctrlOnly);
        if (modsMatch && key == entry.key)
        {
            (this->*entry.handler)(event);
            event->accept();
            return;
        }
    }

    if (ctrlOnly && key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        const char controlByte = static_cast<char>(key - Qt::Key_A + 1);
        m_process->writeInput(QByteArray(1, controlByte));
        event->accept();
        return;
    }

    clearSelection();

    if (key == Qt::Key_Tab)
    {
        // cmd.exe's own Tab completion only recognizes '\' as a path separator and silently does
        // nothing for a token containing '/' (e.g. "resources/tree"). Rewrite the token being
        // completed to use '\' first so cmd's native completion can actually match it. Skip tokens
        // where '/' is the very first character (e.g. "dir /w") -- those are almost certainly a
        // command switch, not a path, and rewriting them could change what Tab does.
        const int liveRow = m_buffer->scrollbackLineCount() + m_buffer->cursorRow();
        const int cursorColumn = m_buffer->cursorColumn();
        const int tokenStart = promptBoundaryColumn(liveRow);

        int start = cursorColumn;
        while (start > tokenStart && cellAtLogicalRow(liveRow, start - 1).character != QLatin1Char(' '))
        {
            --start;
        }

        QString token;
        for (int c = start; c < cursorColumn; ++c)
        {
            token.append(cellAtLogicalRow(liveRow, c).character);
        }

        if (token.contains(QLatin1Char('/')) && !token.startsWith(QLatin1Char('/')))
        {
            token.replace(QLatin1Char('/'), QLatin1Char('\\'));
            m_process->writeInput(QByteArray(cursorColumn - start, '\x08'));
            m_process->writeInput(token.toUtf8());
        }

        m_process->writeInput(QByteArrayLiteral("\t"));
    }
    else if (const auto it = kKeyEscapeSequences.constFind(key); it != kKeyEscapeSequences.constEnd())
    {
        m_process->writeInput(it.value());
    }
    else
    {
        const QString text = event->text();
        if (!text.isEmpty())
        {
            m_process->writeInput(text.toUtf8());
        }
    }

    event->accept();
}

void TerminalWidget::handleWordSelectShortcut(QKeyEvent* event)
{
    const int direction = event->key() == Qt::Key_Left ? -1 : 1;
    extendSelectionByWord(direction);
    m_process->writeInput(direction < 0 ? QByteArrayLiteral("\x1b[1;5D") : QByteArrayLiteral("\x1b[1;5C"));
}

void TerminalWidget::handleCopyShortcut(QKeyEvent*)
{
    if (hasSelection())
    {
        const QString text = selectedText();
        if (!text.isEmpty())
        {
            QGuiApplication::clipboard()->setText(text);
        }
    }
}

void TerminalWidget::handlePasteShortcut(QKeyEvent*)
{
    QString text = QGuiApplication::clipboard()->text();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    m_process->writeInput(text.toUtf8());
    clearSelection();
}

void TerminalWidget::handleShiftSelectShortcut(QKeyEvent* event)
{
    extendSelection(0, event->key() == Qt::Key_Left ? -1 : 1);
}

void TerminalWidget::handleSelectAllShortcut(QKeyEvent*)
{
    selectAll();
}

void TerminalWidget::handleCtrlArrowWordNavShortcut(QKeyEvent* event)
{
    clearSelection();
    m_process->writeInput(event->key() == Qt::Key_Left ? QByteArrayLiteral("\x1b[1;5D") : QByteArrayLiteral("\x1b[1;5C"));
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    return false;
}

void TerminalWidget::wheelEvent(QWheelEvent* event)
{
    const int notches = event->angleDelta().y() / 120;
    if (notches != 0)
    {
        const int maxOffset = m_buffer->scrollbackLineCount();
        m_scrollOffset = std::clamp(m_scrollOffset + notches * kScrollLinesPerNotch, 0, maxOffset);
        update();
    }
    event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    clearSelection();

    if (!m_exitMessage.isEmpty())
    {
        emit restartRequested();
    }

    QWidget::mousePressEvent(event);
}
