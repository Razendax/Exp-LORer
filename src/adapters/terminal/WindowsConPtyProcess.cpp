#include "WindowsConPtyProcess.h"

#include "Error.h"
#include "Logging.h"

#ifdef _WIN32

#include <memory>

#include <Windows.h>

namespace
{
    constexpr int kDefaultColumns = 80;
    constexpr int kDefaultRows = 24;

    HPCON asPseudoConsole(void* handle)
    {
        return static_cast<HPCON>(handle);
    }

    HANDLE asHandle(void* handle)
    {
        return static_cast<HANDLE>(handle);
    }
}

WindowsConPtyProcess::WindowsConPtyProcess(QObject* parent)
    : QObject(parent)
{
}

WindowsConPtyProcess::~WindowsConPtyProcess()
{
    terminate();
}

Result<void> WindowsConPtyProcess::start(const std::filesystem::path& cwd, int columns, int rows, TerminalShellType shellType)
{
    if (m_running)
    {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, "Terminal process is already running"));
    }

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) || !CreatePipe(&outputRead, &outputWrite, nullptr, 0))
    {
        Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create pipes for the terminal process"));
    }

    const COORD size{ static_cast<SHORT>(columns > 0 ? columns : kDefaultColumns),
                       static_cast<SHORT>(rows > 0 ? rows : kDefaultRows) };
    HPCON pseudoConsole = nullptr;
    const HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &pseudoConsole);

    // ConPTY duplicates these two pipe ends internally; the copies on this side are only needed
    // until it owns them.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    if (FAILED(hr))
    {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create the pseudoconsole"));
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
    auto attributeListStorage = std::make_unique<char[]>(attributeListSize);
    startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListStorage.get());

    if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListSize)
        || !UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole,
                                       sizeof(HPCON), nullptr, nullptr))
    {
        ClosePseudoConsole(pseudoConsole);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to initialize the process attribute list"));
    }

    std::wstring commandLine;
    if (shellType == TerminalShellType::PowerShell)
    {
        // Bare executable name with lpApplicationName == nullptr below resolves through the
        // standard CreateProcessW search order, which includes PATH; Windows PowerShell ships with
        // Windows and is on PATH by default. Deliberately not pwsh.exe (PowerShell 7), which isn't
        // guaranteed installed.
        commandLine = L"powershell.exe";
    }
    else
    {
        wchar_t comSpecBuffer[MAX_PATH]{};
        commandLine = (GetEnvironmentVariableW(L"ComSpec", comSpecBuffer, MAX_PATH) != 0) ? comSpecBuffer : L"cmd.exe";
    }

    const std::wstring cwdString = cwd.wstring();
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                                         nullptr, cwd.empty() ? nullptr : cwdString.c_str(), &startupInfo.StartupInfo,
                                         &processInfo);

    DeleteProcThreadAttributeList(startupInfo.lpAttributeList);

    if (!created)
    {
        ClosePseudoConsole(pseudoConsole);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to launch the command shell"));
    }

    CloseHandle(processInfo.hThread); // only the process handle is needed past this point

    // std::thread's constructor can throw (e.g. resource_unavailable_try_again) if the OS can't
    // spawn the thread. By this point the pseudoconsole/process are live OS resources, so a thrown
    // exception must be caught and translated here (Architecture.md §6 "Errors") rather than
    // escaping uncaught and leaking the ConPTY session, handles, and child process.
    try
    {
        m_readerThread = std::thread(&WindowsConPtyProcess::readLoop, this);
    }
    catch (const std::exception& ex)
    {
        TerminateProcess(processInfo.hProcess, 0);
        ClosePseudoConsole(pseudoConsole);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        CloseHandle(processInfo.hProcess);

        Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)) + " ("
                       + ex.what() + ")");
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to start the terminal reader thread"));
    }

    m_pseudoConsole = pseudoConsole;
    m_inputWrite = inputWrite;
    m_outputRead = outputRead;
    m_processHandle = processInfo.hProcess;
    m_running = true;

    Logging::info("WindowsConPtyProcess: process started (" + std::to_string(size.X) + "x" + std::to_string(size.Y) + ")");
    return Result<void>::success();
}

void WindowsConPtyProcess::readLoop()
{
    constexpr DWORD kBufferSize = 4096;
    char buffer[kBufferSize];
    DWORD bytesRead = 0;

    while (ReadFile(asHandle(m_outputRead), buffer, kBufferSize, &bytesRead, nullptr) && bytesRead > 0)
    {
        QByteArray chunk(buffer, static_cast<int>(bytesRead));
        QMetaObject::invokeMethod(this, [this, chunk]() { emit dataReceived(chunk); }, Qt::QueuedConnection);
    }

    DWORD exitCode = 0;
    if (m_processHandle)
    {
        WaitForSingleObject(asHandle(m_processHandle), INFINITE);
        GetExitCodeProcess(asHandle(m_processHandle), &exitCode);
    }

    Logging::info("WindowsConPtyProcess: process exited (code " + std::to_string(exitCode) + ")");

    // Posted rather than done inline: terminate() joins this very thread, which would deadlock if
    // called from within readLoop() itself. Queuing it onto the UI thread lets readLoop() return
    // first (this thread finishes essentially immediately after), so the join there is safe.
    QMetaObject::invokeMethod(
        this,
        [this, exitCode]() {
            terminate();
            emit processExited(static_cast<int>(exitCode));
        },
        Qt::QueuedConnection);
}

void WindowsConPtyProcess::writeInput(const QByteArray& bytes)
{
    if (!m_running || !m_inputWrite)
    {
        return;
    }

    DWORD written = 0;
    WriteFile(asHandle(m_inputWrite), bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

void WindowsConPtyProcess::resize(int columns, int rows)
{
    if (!m_running || !m_pseudoConsole || columns <= 0 || rows <= 0)
    {
        return;
    }

    const COORD size{ static_cast<SHORT>(columns), static_cast<SHORT>(rows) };
    ResizePseudoConsole(asPseudoConsole(m_pseudoConsole), size);
    Logging::debug("WindowsConPtyProcess: resized (" + std::to_string(columns) + "x" + std::to_string(rows) + ")");
}

void WindowsConPtyProcess::terminate()
{
    if (m_pseudoConsole)
    {
        ClosePseudoConsole(asPseudoConsole(m_pseudoConsole)); // also breaks the reader thread's blocking ReadFile
        m_pseudoConsole = nullptr;
    }

    if (m_processHandle)
    {
        TerminateProcess(asHandle(m_processHandle), 0);
    }

    if (m_readerThread.joinable())
    {
        m_readerThread.join();
    }

    if (m_inputWrite)
    {
        CloseHandle(asHandle(m_inputWrite));
        m_inputWrite = nullptr;
    }
    if (m_outputRead)
    {
        CloseHandle(asHandle(m_outputRead));
        m_outputRead = nullptr;
    }
    if (m_processHandle)
    {
        CloseHandle(asHandle(m_processHandle));
        m_processHandle = nullptr;
    }

    m_running = false;
}

#else

WindowsConPtyProcess::WindowsConPtyProcess(QObject* parent)
    : QObject(parent)
{
}

WindowsConPtyProcess::~WindowsConPtyProcess() = default;

Result<void> WindowsConPtyProcess::start(const std::filesystem::path&, int, int, TerminalShellType)
{
    Logging::warn("WindowsConPtyProcess: start failed: " + std::string(toLogString(ErrorCode::IoError)));
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

void WindowsConPtyProcess::writeInput(const QByteArray&)
{
}

void WindowsConPtyProcess::resize(int, int)
{
}

void WindowsConPtyProcess::terminate()
{
}

#endif
