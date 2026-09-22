#include "DirectoryWatchUseCase.h"

#include <utility>

DirectoryWatchUseCase::DirectoryWatchUseCase(IDirectoryWatcher& directoryWatcher)
    : m_directoryWatcher(directoryWatcher)
{
}

Result<DirectoryWatchId> DirectoryWatchUseCase::watch(const std::filesystem::path& directory, std::function<void()> onChanged)
{
    return m_directoryWatcher.watch(directory, std::move(onChanged));
}

void DirectoryWatchUseCase::unwatch(DirectoryWatchId id)
{
    m_directoryWatcher.unwatch(id);
}
