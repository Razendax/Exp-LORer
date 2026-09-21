#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include <QByteArrayView>
#include <QChar>
#include <QColor>
#include <QObject>

// Pure VT/ANSI terminal emulation state (Architecture.md §14.23): a resizable grid of cells, a
// cursor, and a capped scrollback, fed raw pty bytes via feed(). No Qt Widgets/OS calls -- the one
// piece of the integrated-terminal feature that's pure and unit-tested (see
// tests/adapters/AnsiTerminalBufferTest.cpp). TerminalWidget owns one instance and repaints from
// it whenever contentChanged() fires.
class AnsiTerminalBuffer : public QObject
{
    Q_OBJECT

public:
    // An invalid (default-constructed) QColor for foreground/background means "use the widget's
    // default color" -- SGR 39/49 (or a bare reset) reset back to this rather than to a concrete
    // color, so the terminal tracks the host widget's theme.
    struct Cell
    {
        QChar character = QLatin1Char(' ');
        QColor foreground;
        QColor background;
        bool bold = false;
        bool underline = false;
        bool inverse = false;
    };

    explicit AnsiTerminalBuffer(QObject* parent = nullptr);

    // Resets the grid to columns x rows (blank cells, default attributes) and clamps the cursor
    // into range. Content is preserved top-left-aligned as far as the new size allows; cells
    // outside the new bounds are dropped. Called by TerminalWidget::resizeEvent.
    void resize(int columns, int rows);

    int columns() const noexcept { return m_columns; }
    int rows() const noexcept { return m_rows; }

    // row/column are 0-based, row 0 is the topmost currently-visible row. Caller must keep
    // 0 <= row < rows() and 0 <= column < columns().
    const Cell& cellAt(int row, int column) const;

    int scrollbackLineCount() const noexcept { return static_cast<int>(m_scrollback.size()); }
    const std::vector<Cell>& scrollbackLine(int index) const { return m_scrollback[static_cast<std::size_t>(index)]; }

    int cursorRow() const noexcept { return m_cursorRow; }
    int cursorColumn() const noexcept { return m_cursorColumn; }
    bool cursorVisible() const noexcept { return m_cursorVisible; }

    // Runs the byte-oriented parser: printable text, \r/\n/\b, and a practical CSI subset (cursor
    // motion CUU/CUD/CUF/CUB/CUP, erase EL/ED, SGR 0/1/4/7/30-37/39/40-47/49/90-97/100-107, and the
    // DECTCEM ?25h/?25l cursor-visibility toggle) -- no 256-color/truecolor (Architecture.md
    // §14.23). Parser state persists across calls, so a sequence split across two feed() calls (a
    // ConPTY read boundary landing mid-escape-sequence) still parses correctly. Unrecognized escape
    // sequences are consumed and ignored rather than leaking their bytes into the grid as text --
    // including OSC (ESC ]) sequences like the window-title updates cmd.exe/PowerShell send on
    // every prompt, which are consumed through to their BEL or ST terminator.
    void feed(QByteArrayView bytes);

signals:
    void contentChanged();

private:
    enum class ParseState
    {
        Ground,
        Escape,
        CsiParams,
        // OSC (Operating System Command, ESC ]) payload -- consumed and discarded up to its
        // terminator (BEL, or ST i.e. ESC \) rather than leaked into the grid as text. cmd.exe and
        // PowerShell emit these for window-title updates on every prompt.
        Osc,
    };

    void putChar(QChar ch);
    void lineFeed();
    void carriageReturn();
    void backspace();
    void eraseInLine(int mode);
    void eraseInDisplay(int mode);
    void applySgr(const std::vector<int>& params);
    void moveCursor(int deltaRow, int deltaColumn);
    void setCursorPosition(int row, int column);
    void dispatchCsi(char finalByte, const std::vector<int>& params, bool privateMarker);
    void clampCursor();
    Cell blankCell() const;
    Cell currentAttributeCell() const;

    int m_columns = 80;
    int m_rows = 24;
    std::vector<std::vector<Cell>> m_grid;

    std::deque<std::vector<Cell>> m_scrollback;
    static constexpr int kMaxScrollbackLines = 2000;

    int m_cursorRow = 0;
    int m_cursorColumn = 0;
    bool m_cursorVisible = true;

    // Standard terminal "deferred autowrap": writing a printable character into the last column
    // sets this instead of immediately advancing past it, so cursorColumn always stays a valid
    // index. The wrap onto the next row only actually happens if *another* printable character
    // arrives next -- any explicit cursor motion (CR, LF, backspace, CUP/CUU/...) in between clears
    // this flag instead, so content that exactly fills a row's width (a common case, e.g. `dir`
    // output as wide as the console) doesn't spuriously push a blank scrollback line.
    bool m_pendingWrap = false;

    QColor m_currentForeground;
    QColor m_currentBackground;
    bool m_currentBold = false;
    bool m_currentUnderline = false;
    bool m_currentInverse = false;

    ParseState m_parseState = ParseState::Ground;
    std::vector<int> m_csiParams;
    int m_csiCurrentValue = 0;
    bool m_csiCurrentValueSet = false;
    bool m_csiPrivateMarker = false;
};
