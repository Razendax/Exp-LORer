#include "LibGit2Repository.h"

#include <git2.h>

#include <string_view>
#include <utility>

#include "Logging.h"
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
        // Never log git_error_last()->message -- it can embed the repository path (Architecture.md §14.23).
        Logging::warn("LibGit2Repository operation failed: " + std::string(toLogString(ErrorCode::IoError)));
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

    struct ReferenceGuard
    {
        git_reference* ref;
        ~ReferenceGuard() { git_reference_free(ref); }
    };

    struct ObjectGuard
    {
        git_object* object;
        ~ObjectGuard() { git_object_free(object); }
    };

    struct BranchIteratorGuard
    {
        git_branch_iterator* iterator;
        ~BranchIteratorGuard() { git_branch_iterator_free(iterator); }
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
        Logging::debug("LibGit2Repository: no repository found");
        return Result<LibGit2Repository>::failure(Error(ErrorCode::NotFound, "No git repository found at or above " + startDirectoryUtf8));
    }

    Logging::debug("LibGit2Repository: repository opened");
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

    Logging::debug("LibGit2Repository: status computed (" + std::to_string(entries.size()) + " entr" +
                    (entries.size() == 1 ? "y" : "ies") + ")");
    return Result<std::vector<StatusEntry>>::success(std::move(entries));
}

Result<std::vector<std::string>> LibGit2Repository::ignoredPaths(const std::string& relativeDirectoryUtf8) const
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    // RECURSE_IGNORED_DIRS deliberately left off (default) -- a wholly-ignored directory is
    // reported as a single entry instead of walking every file inside it, the same perf win
    // statusForImmediateChildren's own doc comment already credits for untracked folders. Unlike
    // statusForImmediateChildren, no pathspec-scoped rollup onto immediate children is needed here
    // -- every reported path is returned as-is, at whatever depth it was found.
    options.flags = GIT_STATUS_OPT_INCLUDE_IGNORED;

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
        return Result<std::vector<std::string>>::failure(libgit2Error("Failed to compute ignored paths"));
    }
    StatusListGuard guard{ rawList };

    std::vector<std::string> paths;
    const size_t count = git_status_list_entrycount(rawList);
    paths.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const git_status_entry* entry = git_status_byindex(rawList, i);
        if (!entry || !(entry->status & GIT_STATUS_IGNORED))
        {
            continue;
        }

        std::string relativePath = pathForEntry(entry);
        if (relativePath.empty())
        {
            continue;
        }

        paths.push_back(std::move(relativePath));
    }

    return Result<std::vector<std::string>>::success(std::move(paths));
}

Result<std::string> LibGit2Repository::headDisplayName(bool& isDetachedOut) const
{
    isDetachedOut = false;

    git_reference* head = nullptr;
    const int headRc = git_repository_head(&head, m_handle);

    if (headRc == GIT_EUNBORNBRANCH)
    {
        // Zero commits yet -- HEAD is a symbolic reference to the would-be branch, which
        // git_repository_head() refuses to resolve (there's no commit for it to point at). Read the
        // symbolic target directly instead.
        git_reference* symbolic = nullptr;
        const int lookupRc = git_reference_lookup(&symbolic, m_handle, "HEAD");
        if (lookupRc != 0)
        {
            return Result<std::string>::failure(libgit2Error("Failed to resolve unborn HEAD"));
        }
        ReferenceGuard symbolicGuard{ symbolic };

        const char* target = git_reference_symbolic_target(symbolic);
        std::string name = target ? target : std::string();

        constexpr std::string_view prefix = "refs/heads/";
        if (name.rfind(prefix, 0) == 0)
        {
            name = name.substr(prefix.size());
        }

        return Result<std::string>::success(std::move(name));
    }

    if (headRc != 0)
    {
        return Result<std::string>::failure(libgit2Error("Failed to resolve HEAD"));
    }
    ReferenceGuard headGuard{ head };

    const int detachedRc = git_repository_head_detached(m_handle);
    if (detachedRc < 0)
    {
        return Result<std::string>::failure(libgit2Error("Failed to determine HEAD state"));
    }
    isDetachedOut = detachedRc == 1;

    if (isDetachedOut)
    {
        const git_oid* oid = git_reference_target(head);
        if (!oid)
        {
            return Result<std::string>::failure(libgit2Error("Detached HEAD has no target"));
        }
        char shortId[16] = {};
        git_oid_tostr(shortId, sizeof(shortId), oid);
        Logging::debug("LibGit2Repository: HEAD resolved (isDetached=true)");
        return Result<std::string>::success(std::string(shortId));
    }

    const char* branchName = nullptr;
    const int nameRc = git_branch_name(&branchName, head);
    if (nameRc != 0 || !branchName)
    {
        return Result<std::string>::failure(libgit2Error("Failed to resolve branch name"));
    }

    Logging::debug(std::string("LibGit2Repository: HEAD resolved (isDetached=") + (isDetachedOut ? "true)" : "false)"));
    return Result<std::string>::success(std::string(branchName));
}

Result<int> LibGit2Repository::changedFileCount() const
{
    // Empty pathspec is already whole-repo, per statusForImmediateChildren's own contract.
    auto statusResult = statusForImmediateChildren("");
    if (!statusResult)
    {
        return Result<int>::failure(std::move(statusResult).error());
    }

    int count = 0;
    for (const StatusEntry& entry : statusResult.value())
    {
        if (entry.status != GitFileStatus::Ignored)
        {
            ++count;
        }
    }

    return Result<int>::success(count);
}

Result<std::vector<std::string>> LibGit2Repository::listLocalBranches() const
{
    git_branch_iterator* rawIterator = nullptr;
    const int newRc = git_branch_iterator_new(&rawIterator, m_handle, GIT_BRANCH_LOCAL);
    if (newRc != 0)
    {
        return Result<std::vector<std::string>>::failure(libgit2Error("Failed to list branches"));
    }
    BranchIteratorGuard iteratorGuard{ rawIterator };

    std::vector<std::string> names;
    git_reference* branchRef = nullptr;
    git_branch_t branchType = GIT_BRANCH_LOCAL;
    int nextRc = 0;
    while ((nextRc = git_branch_next(&branchRef, &branchType, rawIterator)) == 0)
    {
        const char* name = nullptr;
        if (git_branch_name(&name, branchRef) == 0 && name)
        {
            names.emplace_back(name);
        }
        git_reference_free(branchRef);
    }

    if (nextRc != GIT_ITEROVER)
    {
        return Result<std::vector<std::string>>::failure(libgit2Error("Failed while iterating branches"));
    }

    Logging::debug("LibGit2Repository: branches listed (" + std::to_string(names.size()) + ")");
    return Result<std::vector<std::string>>::success(std::move(names));
}

Result<void> LibGit2Repository::checkoutBranch(const std::string& branchName) const
{
    git_reference* branchRef = nullptr;
    const int lookupRc = git_branch_lookup(&branchRef, m_handle, branchName.c_str(), GIT_BRANCH_LOCAL);
    if (lookupRc != 0)
    {
        return Result<void>::failure(libgit2Error("Branch not found: " + branchName));
    }
    ReferenceGuard branchGuard{ branchRef };

    git_object* target = nullptr;
    const int peelRc = git_reference_peel(&target, branchRef, GIT_OBJECT_COMMIT);
    if (peelRc != 0)
    {
        return Result<void>::failure(libgit2Error("Failed to resolve commit for branch " + branchName));
    }
    ObjectGuard targetGuard{ target };

    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    // Never GIT_CHECKOUT_FORCE -- a checkout that would overwrite a modified tracked file fails
    // instead of discarding it (Architecture.md §14.x's "checkout is never forced").
    options.checkout_strategy = GIT_CHECKOUT_SAFE;
    const int checkoutRc = git_checkout_tree(m_handle, target, &options);
    if (checkoutRc != 0)
    {
        return Result<void>::failure(libgit2Error("Checkout failed (local changes would be overwritten?)"));
    }

    const std::string fullRefName = "refs/heads/" + branchName;
    const int setHeadRc = git_repository_set_head(m_handle, fullRefName.c_str());
    if (setHeadRc != 0)
    {
        return Result<void>::failure(libgit2Error("Checkout succeeded but failed to update HEAD"));
    }

    Logging::info("LibGit2Repository: checkout succeeded");
    return Result<void>::success();
}
