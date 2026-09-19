#include <gtest/gtest.h>

#include "AnsiTerminalBuffer.h"

namespace
{
    QChar charAt(const AnsiTerminalBuffer& buffer, int row, int column)
    {
        return buffer.cellAt(row, column).character;
    }
}

TEST(AnsiTerminalBuffer, DefaultConstructionIsAnEmptyDefaultSizedGrid)
{
    AnsiTerminalBuffer buffer;
    EXPECT_EQ(buffer.columns(), 80);
    EXPECT_EQ(buffer.rows(), 24);
    EXPECT_EQ(buffer.cursorRow(), 0);
    EXPECT_EQ(buffer.cursorColumn(), 0);
    EXPECT_TRUE(buffer.cursorVisible());
    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char(' '));
}

TEST(AnsiTerminalBuffer, ResizeChangesDimensionsAndClampsCursor)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    EXPECT_EQ(buffer.columns(), 10);
    EXPECT_EQ(buffer.rows(), 3);
    EXPECT_EQ(buffer.cursorRow(), 0);
    EXPECT_EQ(buffer.cursorColumn(), 0);
}

TEST(AnsiTerminalBuffer, ResizePreservesTopLeftContent)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("Hi"));

    buffer.resize(20, 5);
    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('H'));
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char('i'));
}

TEST(AnsiTerminalBuffer, PlainTextAdvancesCursorAndWritesCells)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("abc"));

    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('a'));
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char('b'));
    EXPECT_EQ(charAt(buffer, 0, 2), QLatin1Char('c'));
    EXPECT_EQ(buffer.cursorColumn(), 3);
}

TEST(AnsiTerminalBuffer, LineWrapsAtEndOfRow)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(3, 3);
    buffer.feed(QByteArrayView("abcd"));

    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('a'));
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char('b'));
    EXPECT_EQ(charAt(buffer, 0, 2), QLatin1Char('c'));
    EXPECT_EQ(charAt(buffer, 1, 0), QLatin1Char('d'));
    EXPECT_EQ(buffer.cursorRow(), 1);
    EXPECT_EQ(buffer.cursorColumn(), 1);
}

TEST(AnsiTerminalBuffer, CarriageReturnAndLineFeedMoveCursorIndependently)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("ab\r\ncd"));

    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('a'));
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char('b'));
    EXPECT_EQ(charAt(buffer, 1, 0), QLatin1Char('c'));
    EXPECT_EQ(charAt(buffer, 1, 1), QLatin1Char('d'));
}

TEST(AnsiTerminalBuffer, BackspaceMovesCursorBackWithoutErasing)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("ab\b"));

    EXPECT_EQ(buffer.cursorColumn(), 1);
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char('b'));
}

TEST(AnsiTerminalBuffer, ScrollingPastLastRowPushesOldestLineToScrollback)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(5, 2);
    // "\r\n" (not bare "\n"), matching what real terminal output actually sends -- each line here
    // exactly fills the 5-column width, which used to trip a deferred-autowrap bug (see the
    // m_pendingWrap comment in AnsiTerminalBuffer.h) into scrolling an extra phantom blank line.
    buffer.feed(QByteArrayView("11111\r\n22222\r\n33333"));

    ASSERT_EQ(buffer.scrollbackLineCount(), 1);
    EXPECT_EQ(buffer.scrollbackLine(0)[0].character, QLatin1Char('1'));
    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('2'));
    EXPECT_EQ(charAt(buffer, 1, 0), QLatin1Char('3'));
}

TEST(AnsiTerminalBuffer, CursorUpDownForwardBackMoveWithinBounds)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 10);
    buffer.feed(QByteArrayView("\x1b[5;5H")); // CUP to row 5, col 5 (1-indexed)
    EXPECT_EQ(buffer.cursorRow(), 4);
    EXPECT_EQ(buffer.cursorColumn(), 4);

    buffer.feed(QByteArrayView("\x1b[2A")); // CUU 2
    EXPECT_EQ(buffer.cursorRow(), 2);

    buffer.feed(QByteArrayView("\x1b[3B")); // CUD 3
    EXPECT_EQ(buffer.cursorRow(), 5);

    buffer.feed(QByteArrayView("\x1b[2C")); // CUF 2
    EXPECT_EQ(buffer.cursorColumn(), 6);

    buffer.feed(QByteArrayView("\x1b[4D")); // CUB 4
    EXPECT_EQ(buffer.cursorColumn(), 2);
}

TEST(AnsiTerminalBuffer, CursorPositionSequenceSplitAcrossTwoFeedCallsStillParses)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 10);
    buffer.feed(QByteArrayView("\x1b[3;"));
    buffer.feed(QByteArrayView("4H"));

    EXPECT_EQ(buffer.cursorRow(), 2);
    EXPECT_EQ(buffer.cursorColumn(), 3);
}

TEST(AnsiTerminalBuffer, EraseInLineModeTwoClearsEntireLine)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(5, 2);
    buffer.feed(QByteArrayView("abcde"));
    buffer.feed(QByteArrayView("\x1b[2K"));

    for (int c = 0; c < 5; ++c)
    {
        EXPECT_EQ(charAt(buffer, 0, c), QLatin1Char(' '));
    }
}

TEST(AnsiTerminalBuffer, EraseInDisplayModeTwoClearsWholeGrid)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(5, 2);
    buffer.feed(QByteArrayView("abcde\r\nfghij"));
    buffer.feed(QByteArrayView("\x1b[2J"));

    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < 5; ++c)
        {
            EXPECT_EQ(charAt(buffer, r, c), QLatin1Char(' '));
        }
    }
}

TEST(AnsiTerminalBuffer, SgrForegroundColorAppliesToSubsequentCharacters)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("\x1b[31mred"));

    const AnsiTerminalBuffer::Cell& cell = buffer.cellAt(0, 0);
    EXPECT_EQ(cell.character, QLatin1Char('r'));
    EXPECT_EQ(cell.foreground, QColor::fromRgb(0xCD0000));
}

TEST(AnsiTerminalBuffer, SgrResetClearsAttributes)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(10, 3);
    buffer.feed(QByteArrayView("\x1b[1;31mbold\x1b[0mplain"));

    const AnsiTerminalBuffer::Cell& boldCell = buffer.cellAt(0, 0);
    EXPECT_TRUE(boldCell.bold);
    EXPECT_TRUE(boldCell.foreground.isValid());

    const AnsiTerminalBuffer::Cell& plainCell = buffer.cellAt(0, 4);
    EXPECT_FALSE(plainCell.bold);
    EXPECT_FALSE(plainCell.foreground.isValid());
}

TEST(AnsiTerminalBuffer, PrivateModeCursorVisibilityToggle)
{
    AnsiTerminalBuffer buffer;
    buffer.feed(QByteArrayView("\x1b[?25l"));
    EXPECT_FALSE(buffer.cursorVisible());

    buffer.feed(QByteArrayView("\x1b[?25h"));
    EXPECT_TRUE(buffer.cursorVisible());
}

TEST(AnsiTerminalBuffer, UnrecognizedEscapeSequenceIsConsumedNotLeakedAsText)
{
    AnsiTerminalBuffer buffer;
    buffer.resize(20, 3);
    // Only '[' starts a recognized (CSI) sequence -- an OSC-style "ESC ]" is unsupported, so the
    // parser drops back to Ground after consuming just the ESC and ']' bytes. Everything after that
    // (including the BEL, a C0 control byte that's simply ignored) is parsed as if the escape had
    // never happened.
    buffer.feed(QByteArrayView("\x1b]0;hi\x07ok"));

    EXPECT_EQ(charAt(buffer, 0, 0), QLatin1Char('0'));
    EXPECT_EQ(charAt(buffer, 0, 1), QLatin1Char(';'));
    EXPECT_EQ(charAt(buffer, 0, 2), QLatin1Char('h'));
    EXPECT_EQ(charAt(buffer, 0, 3), QLatin1Char('i'));
    EXPECT_EQ(charAt(buffer, 0, 4), QLatin1Char('o'));
    EXPECT_EQ(charAt(buffer, 0, 5), QLatin1Char('k'));
}
