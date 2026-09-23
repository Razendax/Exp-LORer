#include "StandardFolderMonitor.h"

#include "Error.h"
#include "VirtualPaths.h"

#ifdef _WIN32

#include <string_view>
#include <thread>
#include <utility>

#include <Windows.h>

struct StandardFolderMonitor::MonitorState
{
    std::filesystem::path root;
    HANDLE directoryHandle = INVALID_HANDLE_VALUE;
    // Manual-reset event signaled by stopMonitor() to wake the worker thread out of its
    // WaitForMultipleObjects wait. CloseHandle() alone does NOT reliably interrupt a *synchronous*
    // ReadDirectoryChangesW call from another thread (a well-documented Win32 quirk -- it can hang
    // indefinitely), so the directory handle is opened with FILE_FLAG_OVERLAPPED and cancellation
    // goes through this event + CancelIoEx instead.
    HANDLE stopEvent = nullptr;
    std::thread workerThread;
    std::unordered_map<std::wstring, bool> pathIsDirectory;
    std::function<void(const std::vector<FolderChangeEvent>&)> onChanged;
};

namespace
{
    // Seeds the relative-path -> isDirectory map once at startMonitoring so a later Removed event
    // (whose file is already gone by the time it's processed) can still report a best-effort
    // isDirectory -- an accepted heuristic, not a correctness guarantee, same posture as the
    // partial-hash file identity (Architecture.md §8). Unreadable subtrees are skipped, same
    // tolerance StandardFileSystemRepository::listDirectoryRecursive already has.
    void seedDirectoryTypes(const std::filesystem::path& root, std::unordered_map<std::wstring, bool>& pathIsDirectory)
    {
        std::error_code ec;
        auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();

        while (!ec && it != end)
        {
            std::error_code entryEc;
            const bool isDirectory = it->is_directory(entryEc);
            const std::filesystem::path relative = it->path().lexically_relative(root);
            pathIsDirectory[relative.wstring()] = !entryEc && isDirectory;

            it.increment(ec);
        }
    }

    // Decodes one ReadDirectoryChangesW buffer into FolderChangeEvents relative to `root`,
    // updating `pathIsDirectory` as it goes (Architecture.md §14.x): Added/renamed-new insert or
    // refresh the entry, Removed/renamed-old look it up then erase it (falling back to
    // isDirectory=false on a lookup miss, e.g. a rapid create-then-delete race within one read
    // cycle). Renames fold into Removed(old path)+Added(new path) -- no dedicated "renamed" kind.
    std::vector<FolderChangeEvent> decodeBuffer(const char* buffer, DWORD bytesReturned, const std::filesystem::path& root,
                                                 std::unordered_map<std::wstring, bool>& pathIsDirectory)
    {
        std::vector<FolderChangeEvent> events;
        std::size_t offset = 0;

        while (offset < bytesReturned)
        {
            const auto* record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer + offset);
            const std::wstring_view nameView(record->FileName, record->FileNameLength / sizeof(WCHAR));
            const std::filesystem::path relativePath{ nameView };
            const std::wstring key = relativePath.wstring();

            switch (record->Action)
            {
                case FILE_ACTION_ADDED:
                case FILE_ACTION_RENAMED_NEW_NAME:
                {
                    std::error_code ec;
                    const bool isDirectory = std::filesystem::is_directory(root / relativePath, ec);
                    pathIsDirectory[key] = !ec && isDirectory;
                    events.push_back(FolderChangeEvent{ relativePath, FolderChangeKind::Added, !ec && isDirectory });
                    break;
                }
                case FILE_ACTION_REMOVED:
                case FILE_ACTION_RENAMED_OLD_NAME:
                {
                    bool isDirectory = false;
                    if (const auto found = pathIsDirectory.find(key); found != pathIsDirectory.end())
                    {
                        isDirectory = found->second;
                        pathIsDirectory.erase(found);
                    }
                    events.push_back(FolderChangeEvent{ relativePath, FolderChangeKind::Removed, isDirectory });
                    break;
                }
                case FILE_ACTION_MODIFIED:
                default:
                {
                    const auto found = pathIsDirectory.find(key);
                    const bool isDirectory = found != pathIsDirectory.end() && found->second;
                    events.push_back(FolderChangeEvent{ relativePath, FolderChangeKind::Modified, isDirectory });
                    break;
                }
            }

            if (record->NextEntryOffset == 0)
            {
                break;
            }
            offset += record->NextEntryOffset;
        }

        return events;
    }
}

StandardFolderMonitor::StandardFolderMonitor(QObject* parent)
    : QObject(parent)
{
}

StandardFolderMonitor::~StandardFolderMonitor()
{
    for (auto& [id, state] : m_monitors)
    {
        stopMonitor(*state);
    }
}

Result<FolderMonitorId> StandardFolderMonitor::startMonitoring(const std::filesystem::path& rootDirectory,
                                                                 std::function<void(const std::vector<FolderChangeEvent>&)> onChanged)
{
    if (rootDirectory == VirtualPaths::ThisPC)
    {
        return Result<FolderMonitorId>::failure(Error(ErrorCode::InvalidArgument, "Cannot monitor the This PC virtual location"));
    }

    HANDLE handle = CreateFileW(rootDirectory.c_str(), FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return Result<FolderMonitorId>::failure(Error(ErrorCode::NotFound, "Directory does not exist or cannot be monitored"));
    }

    HANDLE stopEvent = CreateEventW(nullptr, /*bManualReset=*/TRUE, /*bInitialState=*/FALSE, nullptr);
    if (stopEvent == nullptr)
    {
        CloseHandle(handle);
        return Result<FolderMonitorId>::failure(Error(ErrorCode::IoError, "Failed to create monitor stop event"));
    }

    auto state = std::make_unique<MonitorState>();
    state->root = rootDirectory;
    state->directoryHandle = handle;
    state->stopEvent = stopEvent;
    state->onChanged = std::move(onChanged);
    seedDirectoryTypes(rootDirectory, state->pathIsDirectory);

    const FolderMonitorId id = m_nextId++;

    // The worker thread captures everything it needs by value/raw pointer rather than the
    // MonitorState* itself: pathIsDirectoryMap is only ever touched by this one thread (safe for
    // its whole lifetime, which stopMonitor() bounds via join() before the state is ever erased),
    // and the queued callback below captures onChangedCopy/events by value so it never dereferences
    // the monitor's state after this thread has posted its last batch (Architecture.md §14.x
    // "Threading" -- no shared mutable state crosses the thread hop except by value).
    HANDLE watchedHandle = handle;
    HANDLE watchedStopEvent = stopEvent;
    std::function<void(const std::vector<FolderChangeEvent>&)> onChangedCopy = state->onChanged;
    std::filesystem::path root = state->root;
    auto* pathIsDirectoryMap = &state->pathIsDirectory;
    StandardFolderMonitor* owner = this;

    state->workerThread = std::thread([watchedHandle, watchedStopEvent, onChangedCopy, root, pathIsDirectoryMap, owner]() {
        char buffer[16384];
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, /*bManualReset=*/TRUE, /*bInitialState=*/FALSE, nullptr);

        while (true)
        {
            ResetEvent(overlapped.hEvent);

            const BOOL issued = ReadDirectoryChangesW(watchedHandle, buffer, sizeof(buffer), /*bWatchSubtree=*/TRUE,
                                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                           FILE_NOTIFY_CHANGE_LAST_WRITE,
                                                       nullptr, &overlapped, nullptr);
            if (!issued && GetLastError() != ERROR_IO_PENDING)
            {
                break;
            }

            HANDLE waitHandles[2] = { overlapped.hEvent, watchedStopEvent };
            const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

            if (waitResult != WAIT_OBJECT_0)
            {
                // Stop requested (or a wait failure) -- cancel the pending read and wait for the
                // cancellation itself to complete before touching the handle again.
                DWORD discarded = 0;
                CancelIoEx(watchedHandle, &overlapped);
                GetOverlappedResult(watchedHandle, &overlapped, &discarded, TRUE);
                break;
            }

            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(watchedHandle, &overlapped, &bytesReturned, FALSE) || bytesReturned == 0)
            {
                break;
            }

            std::vector<FolderChangeEvent> events = decodeBuffer(buffer, bytesReturned, root, *pathIsDirectoryMap);
            if (events.empty())
            {
                continue;
            }

            QMetaObject::invokeMethod(
                owner, [onChangedCopy, events = std::move(events)]() { onChangedCopy(events); }, Qt::QueuedConnection);
        }

        CloseHandle(overlapped.hEvent);
    });

    m_monitors.emplace(id, std::move(state));
    return Result<FolderMonitorId>::success(id);
}

void StandardFolderMonitor::stopMonitoring(FolderMonitorId id)
{
    const auto it = m_monitors.find(id);
    if (it == m_monitors.end())
    {
        return;
    }

    stopMonitor(*it->second);
    m_monitors.erase(it);
}

void StandardFolderMonitor::stopMonitor(MonitorState& state)
{
    if (state.stopEvent != nullptr)
    {
        SetEvent(state.stopEvent);
    }

    if (state.workerThread.joinable())
    {
        state.workerThread.join();
    }

    if (state.directoryHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(state.directoryHandle);
        state.directoryHandle = INVALID_HANDLE_VALUE;
    }

    if (state.stopEvent != nullptr)
    {
        CloseHandle(state.stopEvent);
        state.stopEvent = nullptr;
    }
}

#else

struct StandardFolderMonitor::MonitorState
{
};

StandardFolderMonitor::StandardFolderMonitor(QObject* parent)
    : QObject(parent)
{
}

StandardFolderMonitor::~StandardFolderMonitor() = default;

Result<FolderMonitorId> StandardFolderMonitor::startMonitoring(const std::filesystem::path&,
                                                                 std::function<void(const std::vector<FolderChangeEvent>&)>)
{
    return Result<FolderMonitorId>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

void StandardFolderMonitor::stopMonitoring(FolderMonitorId)
{
}

void StandardFolderMonitor::stopMonitor(MonitorState&)
{
}

#endif
