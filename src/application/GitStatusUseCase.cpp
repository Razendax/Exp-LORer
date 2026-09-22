#include "GitStatusUseCase.h"

#include "IGitStatusProvider.h"

GitStatusUseCase::GitStatusUseCase(IGitStatusProvider& gitStatusProvider)
    : m_gitStatusProvider(gitStatusProvider)
{
}

Result<GitDirectoryStatus> GitStatusUseCase::statusForDirectory(const std::filesystem::path& directory) const
{
    return m_gitStatusProvider.statusForDirectory(directory);
}
