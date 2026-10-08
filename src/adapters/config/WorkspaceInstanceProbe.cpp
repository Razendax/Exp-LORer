#include "WorkspaceInstanceProbe.h"

#include <QCryptographicHash>
#include <QLocalSocket>

namespace
{
    // Short enough that a hung/crashed owner process doesn't make the probe appear to freeze.
    constexpr int kLocalIpcTimeoutMs = 200;
}

namespace WorkspaceInstanceProbe
{
    QString deriveChannelName(const std::filesystem::path& appDataDir, const QString& workspaceId)
    {
        // appDataDir can contain non-ASCII characters (Architecture.md §6) -- round-tripped through
        // wstring/QString rather than path::string(), which would throw on Windows for such paths.
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QString::fromStdWString(appDataDir.wstring()).toUtf8());
        hash.addData(workspaceId.toUtf8());

        return QStringLiteral("ExpLORer-") + QString::fromLatin1(hash.result().toHex());
    }

    bool isOwned(const std::filesystem::path& appDataDir, const QString& workspaceId)
    {
#ifdef _WIN32
        QLocalSocket socket;
        socket.connectToServer(deriveChannelName(appDataDir, workspaceId));
        const bool connected = socket.waitForConnected(kLocalIpcTimeoutMs);
        if (connected)
        {
            socket.disconnectFromServer();
        }
        return connected;
#else
        (void)appDataDir;
        (void)workspaceId;
        return false;
#endif
    }
}
