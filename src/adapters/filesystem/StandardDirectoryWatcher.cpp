#include "StandardDirectoryWatcher.h"

#include <utility>

#include <QFileSystemWatcher>
#include <QString>

#include "Logging.h"
#include "VirtualPaths.h"

StandardDirectoryWatcher::StandardDirectoryWatcher(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &StandardDirectoryWatcher::onDirectoryChanged);
}

StandardDirectoryWatcher::~StandardDirectoryWatcher() = default;

Result<DirectoryWatchId> StandardDirectoryWatcher::watch(const std::filesystem::path& directory, std::function<void()> onChanged)
{
    if (directory == VirtualPaths::ThisPC)
    {
        return Result<DirectoryWatchId>::failure(Error(ErrorCode::InvalidArgument, "Cannot watch the This PC virtual location"));
    }

    const std::wstring pathKey = directory.wstring();
    const DirectoryWatchRegistry::AddOutcome outcome = m_registry.addWatch(pathKey, std::move(onChanged));

    if (outcome.shouldStartOsWatch && !m_watcher->addPath(QString::fromStdWString(pathKey)))
    {
        m_registry.removeWatch(outcome.id);
        Logging::warn("directory watch failed: " + std::string(toLogString(ErrorCode::NotFound)));
        return Result<DirectoryWatchId>::failure(Error(ErrorCode::NotFound, "Directory does not exist or cannot be watched"));
    }

    return Result<DirectoryWatchId>::success(outcome.id);
}

void StandardDirectoryWatcher::unwatch(DirectoryWatchId id)
{
    const DirectoryWatchRegistry::RemoveOutcome outcome = m_registry.removeWatch(id);
    if (outcome.found && outcome.shouldStopOsWatch)
    {
        m_watcher->removePath(QString::fromStdWString(outcome.pathKey));
    }
}

void StandardDirectoryWatcher::onDirectoryChanged(const QString& path)
{
    const std::wstring pathKey = path.toStdWString();
    for (const std::function<void()>& callback : m_registry.callbacksForPath(pathKey))
    {
        callback();
    }
}
