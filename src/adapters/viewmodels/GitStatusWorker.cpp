#include "GitStatusWorker.h"

#include "GitStatusUseCase.h"

GitStatusWorker::GitStatusWorker(GitStatusUseCase& gitStatusUseCase, QObject* parent)
    : QObject(parent)
    , m_gitStatusUseCase(gitStatusUseCase)
{
}

Result<GitDirectoryStatus> GitStatusWorker::compute(const std::filesystem::path& directory) const
{
    return m_gitStatusUseCase.statusForDirectory(directory);
}
