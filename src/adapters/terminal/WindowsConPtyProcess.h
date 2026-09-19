#pragma once

#include <filesystem>

#include <QByteArray>
#include <QObject>

#include "Result.h"

#ifdef _WIN32
#include <thread>
#endif

// Wraps one ConPTY-backed cmd.exe child process (Architecture.md §14.23). Windows-only, same
// "Windows first" posture as ShellContextMenuProvider -- start() fails with ErrorCode::IoError on
// any other platform, and every other member is a no-op there. All blocking I/O on the ConPTY's
// output pipe happens on one background std::thread spawned by start(); results are marshaled back
// to the thread that constructed this QObject via QMetaObject::invokeMethod(..., Qt::
// QueuedConnection) -- the only cross-thread hop, so TerminalWidget never touches the pipe
// directly. writeInput()/resize() are synchronous calls from the UI thread, the same accepted
// posture as the existing synchronous ShellExecuteW call (Architecture.md §14.11).
class WindowsConPtyProcess : public QObject
{
    Q_OBJECT

public:
    explicit WindowsConPtyProcess(QObject* parent = nullptr);
    ~WindowsConPtyProcess() override;

    // Spawns %ComSpec% (falling back to cmd.exe) with working directory cwd and an initial pty
    // size of columns x rows. Fails if a process is already running.
    Result<void> start(const std::filesystem::path& cwd, int columns, int rows);

    bool isRunning() const noexcept { return m_running; }

    void writeInput(const QByteArray& bytes);
    void resize(int columns, int rows);

    // Tears down the pseudoconsole/pipes/process (which unblocks the reader thread) and joins it.
    // Idempotent -- safe to call when not running.
    void terminate();

signals:
    void dataReceived(const QByteArray& bytes);
    void processExited(int exitCode);

private:
#ifdef _WIN32
    void readLoop();

    // Stored as void* (rather than HANDLE/HPCON, both plain pointer typedefs) so this header never
    // has to #include <Windows.h> -- that keeps the heavy Windows headers (and their macro
    // collisions, e.g. min/max) out of TerminalWidget.h and everything else that includes this file
    // transitively. Cast back to the real types in the .cpp, where windows.h is included.
    void* m_pseudoConsole = nullptr;
    void* m_inputWrite = nullptr; // parent writes here -> child's stdin
    void* m_outputRead = nullptr; // parent reads here <- child's stdout/stderr
    void* m_processHandle = nullptr;
    std::thread m_readerThread;
#endif
    bool m_running = false;
};
