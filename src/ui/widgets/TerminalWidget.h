#pragma once

#include <filesystem>
#include <optional>

#include <QChar>
#include <QFont>
#include <QString>
#include <QWidget>

#include "AnsiTerminalBuffer.h"

class WindowsConPtyProcess;

// One ConPTY-backed cmd.exe session rendered as a VT/ANSI terminal (Architecture.md §14.23): owns
// one AnsiTerminalBuffer (parsed grid state) and one WindowsConPtyProcess (the child process),
// created but not started until start() is called. Self-contained like AddressBarWidget -- no
// TabViewModel/use-case dependency; the owning WorkspacePaneWidget supplies the working directory
// to start() and, on restartRequested(), the tab's *current* directory at restart time.
class TerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget* parent = nullptr);

    // No-op if a session is already running. On failure to spawn, shows the same inline
    // "click to restart" state as a process that exited on its own.
    void start(const std::filesystem::path& workingDirectory);

signals:
    // Emitted when the user clicks the terminal while it's showing the "click to restart" state
    // (never started, the process exited, or the last start() call failed).
    void restartRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

    // Without this override, QWidget::event() consumes Tab/Shift+Tab itself (to move keyboard
    // focus to the next/previous widget) before keyPressEvent() ever runs, so the Key_Tab case
    // below would never fire and cmd.exe's own file/folder tab completion could never be reached.
    // Returning false here declines the focus-chain move so the Tab keystroke falls through to
    // keyPressEvent() and gets forwarded to the shell instead, matching how a real terminal behaves.
    bool focusNextPrevChild(bool next) override;

private:
    // Recomputes the cell grid (columns x rows) from the widget's current pixel size and the
    // monospace font metrics; resizes both the buffer and (if running) the pty when it changes.
    // No-op while the widget has no real size yet (e.g. still collapsed inside BottomPanelWidget).
    void recomputeGridSize();

    // Logical row addressing shared by painting and selection: row 0 is the oldest retained
    // scrollback line, logicalRowCount() - 1 the bottom of the live grid.
    int logicalRowCount() const;
    const AnsiTerminalBuffer::Cell& cellAtLogicalRow(int logicalRow, int column) const;

    // On the live input row only, the column just past the first '>' on that row -- '>' can't
    // appear in a Windows path, so it marks where the shell prompt (e.g. "C:\Users\foo>") ends and
    // the actual command text begins. Returns 0 for every other row (no boundary).
    int promptBoundaryColumn(int logicalRow) const;

    // On the live input row only, the column of the last non-space character typed so far (or
    // promptBoundaryColumn(logicalRow) - 1 if nothing has been typed yet) -- forward selection must
    // not walk past this into the untyped blank cells that fill out the rest of the row. Returns
    // columns() - 1 for every other row (no restriction).
    int typedTextEndColumn(int logicalRow) const;

    // typedTextEndColumn(), widened to also cover the selection anchor's own column when the anchor
    // sits on logicalRow -- the anchor is planted at the cursor cell, which is one column past the
    // last typed character, so without this a backward selection could never shrink all the way
    // back to its own starting point via Shift+Right.
    int forwardSelectionBoundary(int logicalRow) const;

    // The last non-space column on logicalRow, or -1 if the row is entirely blank. Used to clamp
    // multi-row selection highlighting to each row's own displayed text instead of the full grid
    // width -- see the comment in paintEvent().
    int lastNonSpaceColumn(int logicalRow) const;

    struct SelectionPoint
    {
        int logicalRow = 0;
        int column = 0;
    };

    bool hasSelection() const;
    void clearSelection();
    void extendSelection(int deltaLogicalRow, int deltaColumn);
    void extendSelectionByWord(int direction);
    void selectAll();
    QString selectedText() const;
    static bool isWordChar(QChar ch);

    AnsiTerminalBuffer* m_buffer = nullptr;
    WindowsConPtyProcess* m_process = nullptr;

    // Keyboard-driven selection state (Architecture.md §14.23) -- unset anchor means no active
    // selection. Owned here rather than in AnsiTerminalBuffer since it's interaction/rendering
    // state, not terminal emulation state.
    std::optional<SelectionPoint> m_selectionAnchor;
    SelectionPoint m_selectionActive;

    QFont m_font;
    int m_currentColumns = 80;
    int m_currentRows = 24;

    // Lines of scrollback revealed above the live grid (0 = viewing the live tail); reset to 0
    // whenever new content arrives, same "snap to bottom on output" behavior as most terminals.
    int m_scrollOffset = 0;

    // Set when the process isn't running (never started, exited, or failed to start); shown
    // centered instead of the grid. Empty while a session is actively running.
    QString m_exitMessage;
};
