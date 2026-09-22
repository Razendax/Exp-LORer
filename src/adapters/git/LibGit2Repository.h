#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GitFileStatus.h"
#include "Result.h"

struct git_repository;

// Thin wrapper over libgit2's git_repository_open_ext/git_status_list_new C API (Architecture.md
// §14.x, "use a wrapper, never the library directly" -- same posture as LibArchiveReader for
// libarchive). Move-only; no copy. Only ever touched from GitStatusWorker's one dedicated
// background thread, so libgit2's global init and any git_repository* use never race across
// threads.
class LibGit2Repository
{
public:
    LibGit2Repository(const LibGit2Repository&) = delete;
    LibGit2Repository& operator=(const LibGit2Repository&) = delete;
    LibGit2Repository(LibGit2Repository&& other) noexcept;
    LibGit2Repository& operator=(LibGit2Repository&& other) noexcept;
    ~LibGit2Repository();

    // Discovers a repository by walking upward from startDirectory. Err(NotFound) if no .git is
    // found in any ancestor.
    static Result<LibGit2Repository> open(const std::filesystem::path& startDirectory);

    // Absolute path to the repository's working directory (empty path if this is a bare repo).
    std::filesystem::path workdirPath() const;

    struct StatusEntry
    {
        // '/'-separated, UTF-8, relative to the repository workdir (git's own path convention) --
        // not necessarily an immediate child of the directory that was scanned; callers filter/
        // rebuild paths as needed (see LibGit2StatusProvider).
        std::string relativePathUtf8;
        GitFileStatus status;
    };

    // Scans only the given repo-relative directory (relativeDirectoryUtf8, '/'-separated UTF-8, no
    // leading/trailing slash, empty for the workdir root) via a pathspec-scoped git_status_list_new
    // -- a real perf win in large repos, since libgit2 only diffs that subtree instead of the whole
    // repo. Untracked/ignored subdirectories are reported as a single entry each (recurse-into-them
    // left off, libgit2's default) rather than walked file by file.
    Result<std::vector<StatusEntry>> statusForImmediateChildren(const std::string& relativeDirectoryUtf8) const;

private:
    explicit LibGit2Repository(git_repository* handle);

    git_repository* m_handle = nullptr;
};
