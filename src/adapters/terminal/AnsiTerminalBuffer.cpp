#include "AnsiTerminalBuffer.h"

#include <algorithm>
#include <array>

namespace
{
    // Standard 16-color ANSI palette (indices 0-7 normal, 0-7 again but "bright" for 90-97/100-107).
    constexpr std::array<QRgb, 8> kNormalPalette = {
        0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
    };
    constexpr std::array<QRgb, 8> kBrightPalette = {
        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };

    QColor paletteColor(int index, bool bright)
    {
        const auto& palette = bright ? kBrightPalette : kNormalPalette;
        return QColor::fromRgb(palette[static_cast<std::size_t>(index)]);
    }
}

AnsiTerminalBuffer::AnsiTerminalBuffer(QObject* parent)
    : QObject(parent)
{
    resize(m_columns, m_rows);
}

AnsiTerminalBuffer::Cell AnsiTerminalBuffer::blankCell() const
{
    return Cell{};
}

AnsiTerminalBuffer::Cell AnsiTerminalBuffer::currentAttributeCell() const
{
    Cell cell;
    cell.foreground = m_currentForeground;
    cell.background = m_currentBackground;
    cell.bold = m_currentBold;
    cell.underline = m_currentUnderline;
    cell.inverse = m_currentInverse;
    return cell;
}

void AnsiTerminalBuffer::resize(int columns, int rows)
{
    if (columns <= 0 || rows <= 0)
    {
        return;
    }

    std::vector<std::vector<Cell>> newGrid(static_cast<std::size_t>(rows), std::vector<Cell>(static_cast<std::size_t>(columns)));

    const int copyRows = std::min(rows, m_rows);
    const int copyColumns = std::min(columns, m_columns);
    for (int r = 0; r < copyRows; ++r)
    {
        for (int c = 0; c < copyColumns; ++c)
        {
            newGrid[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = m_grid.empty()
                ? Cell{}
                : m_grid[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
        }
    }

    m_grid = std::move(newGrid);
    m_columns = columns;
    m_rows = rows;
    m_pendingWrap = false;
    clampCursor();

    emit contentChanged();
}

const AnsiTerminalBuffer::Cell& AnsiTerminalBuffer::cellAt(int row, int column) const
{
    return m_grid[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

void AnsiTerminalBuffer::clampCursor()
{
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
    m_cursorColumn = std::clamp(m_cursorColumn, 0, m_columns - 1);
}

void AnsiTerminalBuffer::lineFeed()
{
    m_pendingWrap = false;

    if (m_cursorRow == m_rows - 1)
    {
        if (static_cast<int>(m_scrollback.size()) >= kMaxScrollbackLines)
        {
            m_scrollback.pop_front();
        }
        m_scrollback.push_back(m_grid.front());
        m_grid.erase(m_grid.begin());
        m_grid.push_back(std::vector<Cell>(static_cast<std::size_t>(m_columns)));
    }
    else
    {
        ++m_cursorRow;
    }
}

void AnsiTerminalBuffer::carriageReturn()
{
    m_pendingWrap = false;
    m_cursorColumn = 0;
}

void AnsiTerminalBuffer::backspace()
{
    m_pendingWrap = false;
    if (m_cursorColumn > 0)
    {
        --m_cursorColumn;
    }
}

void AnsiTerminalBuffer::putChar(QChar ch)
{
    if (m_pendingWrap)
    {
        lineFeed();
        m_cursorColumn = 0;
        m_pendingWrap = false;
    }

    Cell cell = currentAttributeCell();
    cell.character = ch;
    m_grid[static_cast<std::size_t>(m_cursorRow)][static_cast<std::size_t>(m_cursorColumn)] = cell;

    if (m_cursorColumn == m_columns - 1)
    {
        // Deferred autowrap (see the m_pendingWrap comment in the header) -- stay on the last
        // column rather than stepping past it, so a line that exactly fills the row's width
        // doesn't spuriously scroll before the next real character (or a CR/LF) decides what
        // should actually happen next.
        m_pendingWrap = true;
    }
    else
    {
        ++m_cursorColumn;
    }
}

void AnsiTerminalBuffer::moveCursor(int deltaRow, int deltaColumn)
{
    m_pendingWrap = false;
    m_cursorRow = std::clamp(m_cursorRow + deltaRow, 0, m_rows - 1);
    m_cursorColumn = std::clamp(m_cursorColumn + deltaColumn, 0, m_columns - 1);
}

void AnsiTerminalBuffer::setCursorPosition(int row, int column)
{
    m_pendingWrap = false;
    m_cursorRow = std::clamp(row, 0, m_rows - 1);
    m_cursorColumn = std::clamp(column, 0, m_columns - 1);
}

void AnsiTerminalBuffer::eraseInLine(int mode)
{
    std::vector<Cell>& line = m_grid[static_cast<std::size_t>(m_cursorRow)];
    const int from = (mode == 1 || mode == 2) ? 0 : m_cursorColumn;
    const int to = (mode == 0 || mode == 2) ? (m_columns - 1) : m_cursorColumn;
    for (int c = from; c <= to; ++c)
    {
        line[static_cast<std::size_t>(c)] = blankCell();
    }
}

void AnsiTerminalBuffer::eraseInDisplay(int mode)
{
    if (mode == 2)
    {
        for (auto& line : m_grid)
        {
            std::fill(line.begin(), line.end(), blankCell());
        }
        return;
    }

    if (mode == 1)
    {
        for (int r = 0; r < m_cursorRow; ++r)
        {
            std::fill(m_grid[static_cast<std::size_t>(r)].begin(), m_grid[static_cast<std::size_t>(r)].end(), blankCell());
        }
        eraseInLine(1);
        return;
    }

    // Default (mode 0): cursor to end of screen.
    eraseInLine(0);
    for (int r = m_cursorRow + 1; r < m_rows; ++r)
    {
        std::fill(m_grid[static_cast<std::size_t>(r)].begin(), m_grid[static_cast<std::size_t>(r)].end(), blankCell());
    }
}

void AnsiTerminalBuffer::applySgr(const std::vector<int>& paramsIn)
{
    const std::vector<int>& params = paramsIn.empty() ? std::vector<int>{ 0 } : paramsIn;

    for (int p : params)
    {
        if (p == 0)
        {
            m_currentForeground = QColor();
            m_currentBackground = QColor();
            m_currentBold = false;
            m_currentUnderline = false;
            m_currentInverse = false;
        }
        else if (p == 1)
        {
            m_currentBold = true;
        }
        else if (p == 4)
        {
            m_currentUnderline = true;
        }
        else if (p == 7)
        {
            m_currentInverse = true;
        }
        else if (p == 22)
        {
            m_currentBold = false;
        }
        else if (p == 24)
        {
            m_currentUnderline = false;
        }
        else if (p == 27)
        {
            m_currentInverse = false;
        }
        else if (p >= 30 && p <= 37)
        {
            m_currentForeground = paletteColor(p - 30, false);
        }
        else if (p == 39)
        {
            m_currentForeground = QColor();
        }
        else if (p >= 40 && p <= 47)
        {
            m_currentBackground = paletteColor(p - 40, false);
        }
        else if (p == 49)
        {
            m_currentBackground = QColor();
        }
        else if (p >= 90 && p <= 97)
        {
            m_currentForeground = paletteColor(p - 90, true);
        }
        else if (p >= 100 && p <= 107)
        {
            m_currentBackground = paletteColor(p - 100, true);
        }
        // else: unrecognized (256-color/truecolor 38;5;.../38;2;... and anything else) -- ignored.
    }
}

void AnsiTerminalBuffer::dispatchCsi(char finalByte, const std::vector<int>& params, bool privateMarker)
{
    const int first = params.empty() ? 0 : params[0];
    const int count = first > 0 ? first : 1;

    switch (finalByte)
    {
        case 'A':
            moveCursor(-count, 0);
            break;
        case 'B':
            moveCursor(count, 0);
            break;
        case 'C':
            moveCursor(0, count);
            break;
        case 'D':
            moveCursor(0, -count);
            break;
        case 'H':
        case 'f':
        {
            const int row = (!params.empty() && params[0] > 0) ? params[0] : 1;
            const int column = (params.size() > 1 && params[1] > 0) ? params[1] : 1;
            setCursorPosition(row - 1, column - 1);
            break;
        }
        case 'J':
            eraseInDisplay(first);
            break;
        case 'K':
            eraseInLine(first);
            break;
        case 'm':
            applySgr(params);
            break;
        case 'h':
            if (privateMarker && first == 25)
            {
                m_cursorVisible = true;
            }
            break;
        case 'l':
            if (privateMarker && first == 25)
            {
                m_cursorVisible = false;
            }
            break;
        default:
            break; // unrecognized final byte -- ignored
    }
}

void AnsiTerminalBuffer::feed(QByteArrayView bytes)
{
    if (bytes.isEmpty())
    {
        return;
    }

    for (char rawByte : bytes)
    {
        const auto b = static_cast<unsigned char>(rawByte);

        switch (m_parseState)
        {
            case ParseState::Ground:
                if (b == 0x1B) // ESC
                {
                    m_parseState = ParseState::Escape;
                }
                else if (b == '\r')
                {
                    carriageReturn();
                }
                else if (b == '\n')
                {
                    lineFeed();
                }
                else if (b == '\b')
                {
                    backspace();
                }
                else if (b >= 0x20)
                {
                    // Treated one byte at a time (Latin-1-ish) rather than decoded as UTF-8: cmd.exe's
                    // console output encoding is code-page dependent, and a multi-byte UTF-8 sequence
                    // could straddle two ConPTY read chunks anyway. Good enough for ASCII output; not
                    // full Unicode fidelity (Architecture.md §14.23 scope).
                    putChar(QChar(static_cast<char16_t>(b)));
                }
                // else: other C0 control bytes (BEL, TAB, ...) are ignored in v1.
                break;

            case ParseState::Escape:
                if (b == '[')
                {
                    m_parseState = ParseState::CsiParams;
                    m_csiParams.clear();
                    m_csiCurrentValue = 0;
                    m_csiCurrentValueSet = false;
                    m_csiPrivateMarker = false;
                }
                else
                {
                    // Unsupported escape kind (OSC, DCS, single-char escapes, ...) -- drop it.
                    m_parseState = ParseState::Ground;
                }
                break;

            case ParseState::CsiParams:
                if (b == '?' && m_csiParams.empty() && !m_csiCurrentValueSet)
                {
                    m_csiPrivateMarker = true;
                }
                else if (b >= '0' && b <= '9')
                {
                    m_csiCurrentValue = m_csiCurrentValue * 10 + (b - '0');
                    m_csiCurrentValueSet = true;
                }
                else if (b == ';')
                {
                    m_csiParams.push_back(m_csiCurrentValueSet ? m_csiCurrentValue : 0);
                    m_csiCurrentValue = 0;
                    m_csiCurrentValueSet = false;
                }
                else if (b >= 0x40 && b <= 0x7E)
                {
                    // Final byte.
                    m_csiParams.push_back(m_csiCurrentValueSet ? m_csiCurrentValue : 0);
                    dispatchCsi(static_cast<char>(b), m_csiParams, m_csiPrivateMarker);
                    m_parseState = ParseState::Ground;
                }
                // else: stray byte inside a CSI sequence (e.g. an intermediate we don't support) --
                // ignored, stay in CsiParams.
                break;
        }
    }

    emit contentChanged();
}
