#include "LibGit2Repository.h"

#include <git2.h>

#include <utility>

#include "PathUtf8.h"

namespace
{
    // Constructed on first LibGit2Repository::open() call. Only ever touched from the one
    // dedicated background thread this feature introduces (GitStatusWorker), so no cross-thread
    // init race to worry about.
    struct LibGit2InitGuard
    {
        LibGit2InitGuard() { git_libgit2_init(); }
        ~LibGit2InitGuard() { git_libgit2_shutdown(); }
    };

    void ensureLibGit2Initialized()
    {
        static LibGit2InitGuard guard;
        (void)guard;
    }

    Error libgit2Error(const std::string& fallback)
    {
        const git_error* error = git_error_last();
        return Error(ErrorCode::IoError, error && error->message ? std::string(error->message) : fallback);
    }

    // Highest-priority-first classification (see GitFileStatus.h) -- collapses whichever git2
    // status bits are set on one entry down to a single badge value, worktree status winning over
    // index status when both are present.
    GitFileStatus classify(unsigned int flags)
    {
        if (flags & GIT_STATUS_CONFLICTED)
        {
            return GitFileStatus::Conflicted;
        }
        if (flags & GIT_STATUS_WT_NEW)
        {
            return GitFileStatus::Untracked;
        }
        if (flags & GIT_STATUS_IGNORED)
        {
            return GitFileStatus::Ignored;
        }
        if (flags & GIT_STATUS_WT_DELETED)
        {
            return GitFileStatus::DeletedUnstaged;
        }
        if (flags & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_TYPECHANGE))
        {
            return GitFileStatus::ModifiedUnstaged;
        }
        if (flags & GIT_STATUS_INDEX_DELETED)
        {
            return GitFileStatus::DeletedStaged;
        }
        if (flags & GIT_STATUS_INDEX_NEW)
        {
            return GitFileStatus::AddedStaged;
        }
        // GIT_STATUS_INDEX_MODIFIED / GIT_STATUS_INDEX_TYPECHANGE -- the only bits left that
        // git_status_list_new (with our option flags) would ever set on a reported entry.
        return GitFileStatus::ModifiedStaged;
    }

    std::string pathForEntry(const git_status_entry* entry)
    {
        if (entry->index_to_workdir)
        {
            if (entry->index_to_workdir->new_file.path)
            {
                return entry->index_to_workdir->new_file.path;
            }
            if (entry->index_to_workdir->old_file.path)
            {
                return entry->index_to_workdir->old_file.path;
            }
        }
        if (entry->head_to_index)
        {
            if (entry->head_to_index->new_file.path)
            {
                return entry->head_to_index->new_file.path;
            }
            if (entry->head_to_index->old_file.path)
            {
                return entry->head_to_index->old_file.path;
            }
        }
        return {};
    }

    struct StatusListGuard
    {
        git_status_list* list;
        ~StatusListGuard() { git_status_list_free(list); }
    };
}

LibGit2Repository::LibGit2Repository(git_repository* handle)
    : m_handle(handle)
{
}

LibGit2Repository::LibGit2Repository(LibGit2Repository&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr))
{
}

LibGit2Repository& LibGit2Repository::operator=(LibGit2Repository&& other) noexcept
{
    if (this != &other)
    {
        if (m_handle)
        {
            git_repository_free(m_handle);
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

LibGit2Repository::~LibGit2Repository()
{
    if (m_handle)
    {
        git_repository_free(m_handle);
    }
}

Result<LibGit2Repository> LibGit2Repository::open(const std::filesystem::path& startDirectory)
{
    ensureLibGit2Initialized();

    git_repository* handle = nullptr;
    const std::string startDirectoryUtf8 = PathUtf8::toUtf8(startDirectory);
    const int rc = git_repository_open_ext(&handle, startDirectoryUtf8.c_str(), 0, nullptr);
    if (rc != 0)
    {
        return Result<LibGit2Repository>::failure(Error(ErrorCode::NotFound, "No git repository found at or above " + startDirectoryUtf8));
    }

    return Result<LibGit2Repository>::success(LibGit2Repository(handle));
}

std::filesystem::path LibGit2Repository::workdirPath() const
{
    const char* workdir = git_repository_workdir(m_handle);
    if (!workdir)
    {
        return {};
    }
    const std::u8string utf8(reinterpret_cast<const char8_t*>(workdir));
    return std::filesystem::path(utf8);
}

Result<std::vector<LibGit2Repository::StatusEntry>> LibGit2Repository::statusForImmediateChildren(
    const std::string& relativeDirectoryUtf8) const
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    // Deliberately NOT setting RECURSE_UNTRACKED_DIRS/RECURSE_IGNORED_DIRS -- their default (off)
    // is what makes an entirely-untracked/ignored subfolder report as a single entry rather than
    // walking every file inside it, a real perf win in large repos (LibGit2StatusProvider's rollup
    // would fold all those individual entries onto the same subfolder anyway).
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_INCLUDE_IGNORED;

    char* pathspecStrings[1] = { const_cast<char*>(relativeDirectoryUtf8.c_str()) };
    if (!relativeDirectoryUtf8.empty())
    {
        options.pathspec.strings = pathspecStrings;
        options.pathspec.count = 1;
    }

    git_status_list* rawList = nullptr;
    const int rc = git_status_list_new(&rawList, m_handle, &options);
    if (rc != 0)
    {
        return Result<std::vector<StatusEntry>>::failure(libgit2Error("Failed to compute git status"));
    }
    StatusListGuard guard{ rawList };

    std::vector<StatusEntry> entries;
    const size_t count = git_status_list_entrycount(rawList);
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const git_status_entry* entry = git_status_byindex(rawList, i);
        if (!entry)
        {
            continue;
        }

        std::string relativePath = pathForEntry(entry);
        if (relativePath.empty())
        {
            continue;
        }

        entries.push_back(StatusEntry{ std::move(relativePath), classify(entry->status) });
    }

    return Result<std::vector<StatusEntry>>::success(std::move(entries));
}
