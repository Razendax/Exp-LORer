#pragma once

#include <filesystem>

#include <QObject>

#include "GitDirectoryStatus.h"
#include "Result.h"

class GitStatusUseCase;

// Runs GitStatusUseCase::statusForDirectory on whatever thread it has been moved to
// (Architecture.md §5/§14.x) -- libgit2's status scan is disk/.git I/O and must never run on the
// Qt UI thread. Owned and driven by GitStatusService, same shape as
// FilePreviewViewModel/FilePreviewWorker.
class GitStatusWorker : public QObject
{
    Q_OBJECT

public:
    explicit GitStatusWorker(GitStatusUseCase& gitStatusUseCase, QObject* parent = nullptr);

    Result<GitDirectoryStatus> compute(const std::filesystem::path& directory) const;

private:
    GitStatusUseCase& m_gitStatusUseCase;
};
