#include "TerminalWidget.h"

#include <algorithm>

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include "AnsiTerminalBuffer.h"
#include "WindowsConPtyProcess.h"

namespace
{
    const QColor kDefaultForeground(0xE5, 0xE5, 0xE5);
    const QColor kDefaultBackground(0x0C, 0x0C, 0x0C);
    constexpr int kScrollLinesPerNotch = 3;
}

TerminalWidget::TerminalWidget(QWidget* parent)
    : QWidget(parent)
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

    const auto result = m_process->start(workingDirectory, m_currentColumns, m_currentRows);
    if (!result)
    {
        m_exitMessage = QString::fromStdString(result.error().message) + tr(" — click to restart");
    }

    update();
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

    auto cellAtLogicalRow = [&](int logicalRow, int col) -> const AnsiTerminalBuffer::Cell& {
        if (logicalRow < scrollbackCount)
        {
            return m_buffer->scrollbackLine(logicalRow)[static_cast<std::size_t>(col)];
        }
        return m_buffer->cellAt(logicalRow - scrollbackCount, col);
    };

    for (int visibleRow = 0; visibleRow < m_buffer->rows(); ++visibleRow)
    {
        const int logicalRow = scrollbackCount - m_scrollOffset + visibleRow;
        if (logicalRow < 0 || logicalRow >= scrollbackCount + m_buffer->rows())
        {
            continue;
        }

        const int y = visibleRow * charHeight;
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

    if (event->modifiers() & Qt::ControlModifier)
    {
        const int key = event->key();
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
        {
            const char controlByte = static_cast<char>(key - Qt::Key_A + 1);
            m_process->writeInput(QByteArray(1, controlByte));
            event->accept();
            return;
        }
    }

    switch (event->key())
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_process->writeInput(QByteArrayLiteral("\r"));
            break;
        case Qt::Key_Backspace:
            m_process->writeInput(QByteArrayLiteral("\x08"));
            break;
        case Qt::Key_Tab:
            m_process->writeInput(QByteArrayLiteral("\t"));
            break;
        case Qt::Key_Escape:
            m_process->writeInput(QByteArrayLiteral("\x1b"));
            break;
        case Qt::Key_Up:
            m_process->writeInput(QByteArrayLiteral("\x1b[A"));
            break;
        case Qt::Key_Down:
            m_process->writeInput(QByteArrayLiteral("\x1b[B"));
            break;
        case Qt::Key_Right:
            m_process->writeInput(QByteArrayLiteral("\x1b[C"));
            break;
        case Qt::Key_Left:
            m_process->writeInput(QByteArrayLiteral("\x1b[D"));
            break;
        case Qt::Key_Home:
            m_process->writeInput(QByteArrayLiteral("\x1b[H"));
            break;
        case Qt::Key_End:
            m_process->writeInput(QByteArrayLiteral("\x1b[F"));
            break;
        case Qt::Key_PageUp:
            m_process->writeInput(QByteArrayLiteral("\x1b[5~"));
            break;
        case Qt::Key_PageDown:
            m_process->writeInput(QByteArrayLiteral("\x1b[6~"));
            break;
        case Qt::Key_Delete:
            m_process->writeInput(QByteArrayLiteral("\x1b[3~"));
            break;
        case Qt::Key_Insert:
            m_process->writeInput(QByteArrayLiteral("\x1b[2~"));
            break;
        default:
        {
            const QString text = event->text();
            if (!text.isEmpty())
            {
                m_process->writeInput(text.toUtf8());
            }
            break;
        }
    }

    event->accept();
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

    if (!m_exitMessage.isEmpty())
    {
        emit restartRequested();
    }

    QWidget::mousePressEvent(event);
}
