#pragma once

#include <filesystem>

#include <QFont>
#include <QString>
#include <QWidget>

class AnsiTerminalBuffer;
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

private:
    // Recomputes the cell grid (columns x rows) from the widget's current pixel size and the
    // monospace font metrics; resizes both the buffer and (if running) the pty when it changes.
    // No-op while the widget has no real size yet (e.g. still collapsed inside BottomPanelWidget).
    void recomputeGridSize();

    AnsiTerminalBuffer* m_buffer = nullptr;
    WindowsConPtyProcess* m_process = nullptr;

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
