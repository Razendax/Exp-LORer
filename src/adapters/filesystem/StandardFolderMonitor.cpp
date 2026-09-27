#include "StandardFolderMonitor.h"

#include "Error.h"
#include "VirtualPaths.h"

#ifdef _WIN32

#include <atomic>
#include <string_view>
#include <thread>
#include <utility>

#include <QMetaObject>

#include <Windows.h>

struct StandardFolderMonitor::MonitorState
{
    std::filesystem::path root;
    // Written once by the worker thread (as soon as CreateFileW/CreateEventW succeed) and read by
    // whichever thread later stops this monitor (stopMonitoring()'s detached cleanup thread, or
    // stopMonitorBlocking() from the destructor) -- atomic since those are two different threads
    // with no other synchronization between them. HANDLE is pointer-sized, so std::atomic<HANDLE>
    // is lock-free on every platform this ships on.
    std::atomic<HANDLE> directoryHandle{ INVALID_HANDLE_VALUE };
    std::atomic<HANDLE> stopEvent{ nullptr };
    // Set only by stopMonitorBlocking() (the destructor's shutdown path) if it runs while the
    // worker thread is still inside CreateFileW/CreateEventW, i.e. before directoryHandle/stopEvent
    // are published above -- the only caller that can race the open, since a monitor's
    // FolderMonitorId is never handed to anyone (so stopMonitoring(id) can never be called for it)
    // until after a successful open. Checked by the worker right after publishing the handle/event
    // so it exits immediately instead of proceeding to watch a monitor nobody wants anymore,
    // leaving stopMonitorBlocking to close the handle/event once its own join() unblocks.
    std::atomic<bool> stopRequested{ false };
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
        stopMonitorBlocking(*state);
    }
}

void StandardFolderMonitor::startMonitoring(const std::filesystem::path& rootDirectory,
                                             std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                                             std::function<void(Result<FolderMonitorId>)> onStarted)
{
    if (rootDirectory == VirtualPaths::ThisPC)
    {
        onStarted(Result<FolderMonitorId>::failure(Error(ErrorCode::InvalidArgument, "Cannot monitor the This PC virtual location")));
        return;
    }

    auto state = std::make_unique<MonitorState>();
    state->root = rootDirectory;
    state->onChanged = std::move(onChanged);

    const FolderMonitorId id = m_nextId++;
    MonitorState* statePtr = state.get();
    m_monitors.emplace(id, std::move(state));

    // CreateFileW below is disk/volume I/O -- can block for a long time against an unresponsive
    // network share -- and must never run on the Qt UI thread (CLAUDE.md's threading rule), so it
    // now happens on this worker thread instead of inline in this function as before. onStarted
    // reports the outcome once the handle either opens or fails to, marshaled back the same way
    // onChanged's batches already are (QMetaObject::invokeMethod on `owner`, Qt::QueuedConnection).
    StandardFolderMonitor* owner = this;
    std::filesystem::path root = rootDirectory;

    statePtr->workerThread = std::thread([owner, statePtr, id, root, onStarted = std::move(onStarted)]() {
        HANDLE handle = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            QMetaObject::invokeMethod(
                owner,
                [owner, id, onStarted]() {
                    onStarted(Result<FolderMonitorId>::failure(Error(ErrorCode::NotFound, "Directory does not exist or cannot be monitored")));
                    owner->discardMonitor(id);
                },
                Qt::QueuedConnection);
            return;
        }

        HANDLE stopEvent = CreateEventW(nullptr, /*bManualReset=*/TRUE, /*bInitialState=*/FALSE, nullptr);
        if (stopEvent == nullptr)
        {
            CloseHandle(handle);
            QMetaObject::invokeMethod(
                owner,
                [owner, id, onStarted]() {
                    onStarted(Result<FolderMonitorId>::failure(Error(ErrorCode::IoError, "Failed to create monitor stop event")));
                    owner->discardMonitor(id);
                },
                Qt::QueuedConnection);
            return;
        }

        // Published for stopMonitorBlocking()/stopMonitoring() before the stopRequested check below
        // -- see MonitorState::stopRequested's comment for why this ordering matters.
        statePtr->directoryHandle = handle;
        statePtr->stopEvent = stopEvent;

        if (statePtr->stopRequested)
        {
            // App shutdown asked us to stop while still opening -- stopMonitorBlocking already owns
            // closing directoryHandle/stopEvent once its own join() (blocked on this thread) returns.
            return;
        }

        QMetaObject::invokeMethod(
            owner, [onStarted, id]() { onStarted(Result<FolderMonitorId>::success(id)); }, Qt::QueuedConnection);

        // Seeded here (not in startMonitoring()) since the recursive walk can be slow on a large
        // tree and must not run on the Qt UI thread either.
        seedDirectoryTypes(root, statePtr->pathIsDirectory);

        char buffer[16384];
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, /*bManualReset=*/TRUE, /*bInitialState=*/FALSE, nullptr);

        while (true)
        {
            ResetEvent(overlapped.hEvent);

            const BOOL issued = ReadDirectoryChangesW(handle, buffer, sizeof(buffer), /*bWatchSubtree=*/TRUE,
                                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                           FILE_NOTIFY_CHANGE_LAST_WRITE,
                                                       nullptr, &overlapped, nullptr);
            if (!issued && GetLastError() != ERROR_IO_PENDING)
            {
                break;
            }

            HANDLE waitHandles[2] = { overlapped.hEvent, stopEvent };
            const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

            if (waitResult != WAIT_OBJECT_0)
            {
                // Stop requested (or a wait failure) -- cancel the pending read and wait for the
                // cancellation itself to complete before touching the handle again.
                DWORD discarded = 0;
                CancelIoEx(handle, &overlapped);
                GetOverlappedResult(handle, &overlapped, &discarded, TRUE);
                break;
            }

            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(handle, &overlapped, &bytesReturned, FALSE))
            {
                break;
            }

            if (bytesReturned == 0)
            {
                // A successful completion with zero bytes is ReadDirectoryChangesW's documented
                // signal that the fixed-size buffer overflowed -- too many changes arrived in one
                // cycle to fit. Some changes were lost, but the watch itself is still healthy; just
                // re-issue the read instead of tearing the monitor down.
                continue;
            }

            std::vector<FolderChangeEvent> events = decodeBuffer(buffer, bytesReturned, root, statePtr->pathIsDirectory);
            if (events.empty())
            {
                continue;
            }

            QMetaObject::invokeMethod(
                owner, [onChangedCopy = statePtr->onChanged, events = std::move(events)]() { onChangedCopy(events); }, Qt::QueuedConnection);
        }

        CloseHandle(overlapped.hEvent);
    });
}

void StandardFolderMonitor::stopMonitoring(FolderMonitorId id)
{
    const auto it = m_monitors.find(id);
    if (it == m_monitors.end())
    {
        return;
    }

    std::unique_ptr<MonitorState> state = std::move(it->second);
    m_monitors.erase(it);

    if (HANDLE stopEvent = state->stopEvent.load())
    {
        SetEvent(stopEvent);
    }

    // workerThread.join() can block for as long as CancelIoEx/GetOverlappedResult takes to resolve
    // against an unresponsive network share's redirector -- handed off to its own throwaway,
    // detached thread so stopMonitoring() itself never blocks the calling (UI) thread. Safe to
    // detach: `state` (the handle, the event, the worker thread object) is moved in wholesale,
    // touches nothing else, and reports nothing back.
    std::thread([state = std::move(state)]() mutable {
        if (state->workerThread.joinable())
        {
            state->workerThread.join();
        }
        if (const HANDLE handle = state->directoryHandle.load(); handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
        if (const HANDLE stopEvent = state->stopEvent.load())
        {
            CloseHandle(stopEvent);
        }
    }).detach();
}

void StandardFolderMonitor::stopMonitorBlocking(MonitorState& state)
{
    state.stopRequested = true;

    if (const HANDLE stopEvent = state.stopEvent.load())
    {
        SetEvent(stopEvent);
    }

    if (state.workerThread.joinable())
    {
        state.workerThread.join();
    }

    if (const HANDLE handle = state.directoryHandle.load(); handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        state.directoryHandle = INVALID_HANDLE_VALUE;
    }

    if (const HANDLE stopEvent = state.stopEvent.load())
    {
        CloseHandle(stopEvent);
        state.stopEvent = nullptr;
    }
}

void StandardFolderMonitor::discardMonitor(FolderMonitorId id)
{
    const auto it = m_monitors.find(id);
    if (it == m_monitors.end())
    {
        return;
    }

    // The worker that posted this call is already on its way out (it never proceeds past posting
    // this) -- join() here is a formality, never a real wait.
    if (it->second->workerThread.joinable())
    {
        it->second->workerThread.join();
    }

    m_monitors.erase(it);
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

void StandardFolderMonitor::startMonitoring(const std::filesystem::path&,
                                             std::function<void(const std::vector<FolderChangeEvent>&)>,
                                             std::function<void(Result<FolderMonitorId>)> onStarted)
{
    onStarted(Result<FolderMonitorId>::failure(Error(ErrorCode::IoError, "Not supported on this platform")));
}

void StandardFolderMonitor::stopMonitoring(FolderMonitorId)
{
}

void StandardFolderMonitor::stopMonitorBlocking(MonitorState&)
{
}

void StandardFolderMonitor::discardMonitor(FolderMonitorId)
{
}

#endif
