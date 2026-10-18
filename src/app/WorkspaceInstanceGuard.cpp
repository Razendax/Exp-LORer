#include "WorkspaceInstanceGuard.h"

#include <string>
#include <utility>

#include <QLocalServer>
#include <QLocalSocket>

#include "ExceptionHandler.h"
#include "WorkspaceInstanceProbe.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    // Waiting this long for a connect/write/read is generous for a local named-pipe round trip
    // (same machine, no network) but short enough that a hung/crashed owner process doesn't make a
    // losing launch appear to freeze.
    constexpr int kLocalIpcTimeoutMs = 500;
}

WorkspaceInstanceGuard::WorkspaceInstanceGuard(std::filesystem::path appDataDir, QString workspaceId, QObject* parent)
    : QObject(parent)
    , m_appDataDir(std::move(appDataDir))
    , m_workspaceId(std::move(workspaceId))
    , m_channelName(WorkspaceInstanceProbe::deriveChannelName(m_appDataDir, m_workspaceId))
{
}

WorkspaceInstanceGuard::~WorkspaceInstanceGuard()
{
#ifdef _WIN32
    if (m_mutexHandle)
    {
        ReleaseMutex(static_cast<HANDLE>(m_mutexHandle));
        CloseHandle(static_cast<HANDLE>(m_mutexHandle));
    }
#endif
}

bool WorkspaceInstanceGuard::tryAcquire()
{
#ifdef _WIN32
    HANDLE handle = CreateMutexW(nullptr, TRUE, reinterpret_cast<const wchar_t*>(m_channelName.utf16()));
    if (!handle)
    {
        // A genuine OS-level failure, not "another instance owns it" -- GetLastError() here is
        // whatever CreateMutexW itself failed with (e.g. a resource-exhaustion or handle-table
        // error), never ERROR_ALREADY_EXISTS (that case always returns a valid handle). Surfacing
        // this silently as "lost the race" would make the app exit with no window and no
        // explanation, so report it the same way any other unrecoverable startup failure is
        // reported.
        const DWORD error = GetLastError();
        ExceptionHandler::handleFatal("Failed to create the single-instance mutex (Windows error " + std::to_string(error) + ")");
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(handle);

        QLocalSocket socket;
        socket.connectToServer(m_channelName);
        if (socket.waitForConnected(kLocalIpcTimeoutMs))
        {
            socket.write("activate");
            socket.waitForBytesWritten(kLocalIpcTimeoutMs);
            socket.disconnectFromServer();
        }

        return false;
    }

    m_mutexHandle = handle;

    // Clears any stale server entry a previous crash of this exact (appDataDir, workspaceId) might
    // have left behind, before listening fresh -- Qt's own recommended pattern for QLocalServer.
    QLocalServer::removeServer(m_channelName);
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* socket = m_server->nextPendingConnection())
        {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                socket->readAll();
                emit activationRequested();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    m_server->listen(m_channelName);

    return true;
#else
    return true;
#endif
}

bool WorkspaceInstanceGuard::isOwned(const std::filesystem::path& appDataDir, const QString& workspaceId)
{
    return WorkspaceInstanceProbe::isOwned(appDataDir, workspaceId);
}
