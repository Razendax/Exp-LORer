#pragma once

#include <filesystem>

#include <QObject>
#include <QString>

class QLocalServer;

// Per-(appDataDir, workspaceId) single-instance guard (Architecture.md §6/§14.60): a Windows named
// mutex plus a QLocalServer/QLocalSocket "activate" channel, both named from a hash of the pair so
// two different workspaces -- or two different --data-dir overrides -- never contend with each
// other; only a second process targeting the exact same (appDataDir, workspaceId) loses the race.
//
// Not implemented for non-Windows yet (tryAcquire() always succeeds there) -- consistent with
// "Windows-first, Linux later" and the existing #ifdef _WIN32 scoping of resolveDataDirOverride in
// main.cpp.
class WorkspaceInstanceGuard : public QObject
{
    Q_OBJECT

public:
    WorkspaceInstanceGuard(std::filesystem::path appDataDir, QString workspaceId, QObject* parent = nullptr);
    ~WorkspaceInstanceGuard() override;

    // Attempts to become the owning instance for this (appDataDir, workspaceId) pair. On success,
    // starts listening for "activate" messages from future losing processes and returns true -- the
    // caller may proceed to construct CompositionRoot/MainWindow. On failure (another process
    // already owns it), forwards one "activate" message to that process and returns false -- the
    // caller must exit immediately without constructing CompositionRoot or any window.
    bool tryAcquire();

    // Non-destructive probe: true if some process (this one or another) currently owns the guard
    // name for `appDataDir`/`workspaceId`, without creating or acquiring anything -- used by
    // WorkspaceManagerDialog to block deleting a workspace that's open right now.
    static bool isOwned(const std::filesystem::path& appDataDir, const QString& workspaceId);

signals:
    // Emitted on the winning instance when a losing process forwards an "activate" message.
    void activationRequested();

private:
    std::filesystem::path m_appDataDir;
    QString m_workspaceId;
    QString m_channelName;

    // Opaque HANDLE (Windows named mutex), stored as void* so this header never needs <Windows.h>.
    void* m_mutexHandle = nullptr;

    QLocalServer* m_server = nullptr;
};
