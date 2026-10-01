#include "LibGit2StatusProvider.h"

#include <string_view>
#include <system_error>
#include <utility>

#include "LibGit2Repository.h"

namespace
{
    namespace fs = std::filesystem;

    // UTF-8, '/'-separated -- git's own path convention, unlike PathUtf8::toUtf8() which keeps the
    // platform's native separators (backslashes on Windows).
    std::string toGitRelativeUtf8(const fs::path& path)
    {
        const std::u8string encoded = path.generic_u8string();
        return std::string(encoded.begin(), encoded.end());
    }

    fs::path fromGitUtf8(std::string_view utf8)
    {
        return fs::path(std::u8string(utf8.begin(), utf8.end()));
    }

    // Folder-rollup priority: lower wins when more than one nested entry rolls up onto the same
    // immediate-child directory. Deliberately its own order rather than reusing GitFileStatus.h's
    // declared (per-file bit-collapsing) order -- that order ranks Ignored above the Modified/
    // Added/Deleted statuses, which is right for collapsing simultaneous flags on one file but
    // wrong here: an ignored build artifact sitting next to a genuinely modified tracked file
    // inside the same folder shouldn't hide the modification behind an "Ignored" badge on the
    // folder row.
    int rollupPriority(GitFileStatus status)
    {
        switch (status)
        {
            case GitFileStatus::Conflicted:
                return 0;
            case GitFileStatus::Untracked:
                return 1;
            case GitFileStatus::DeletedUnstaged:
                return 2;
            case GitFileStatus::ModifiedUnstaged:
                return 3;
            case GitFileStatus::DeletedStaged:
                return 4;
            case GitFileStatus::AddedStaged:
                return 5;
            case GitFileStatus::ModifiedStaged:
                return 6;
            case GitFileStatus::Ignored:
                return 7;
        }
        return 8;
    }

    // Records `status` for `path`, keeping whichever status already at that key outranks it
    // (lower rollupPriority()) if one is already present -- so a folder that rolls up more than
    // one nested change ends up showing its most important one.
    void recordStatus(GitStatusByPathMap& statusByPath, const fs::path& path, GitFileStatus status)
    {
        const auto existing = statusByPath.find(path);
        if (existing == statusByPath.end() || rollupPriority(status) < rollupPriority(existing->second))
        {
            statusByPath[path] = status;
        }
    }
}

Result<GitDirectoryStatus> LibGit2StatusProvider::statusForDirectory(const std::filesystem::path& directory) const
{
    auto repoResult = LibGit2Repository::open(directory);
    if (!repoResult)
    {
        // No repository at or above `directory` -- tolerated by blanking (Architecture.md's
        // driveSpaceInfo posture), not an error.
        return Result<GitDirectoryStatus>::success(GitDirectoryStatus{});
    }

    LibGit2Repository repository = std::move(repoResult).value();
    const fs::path workdir = repository.workdirPath();
    if (workdir.empty())
    {
        // Bare repository -- no working tree to show status for.
        return Result<GitDirectoryStatus>::success(GitDirectoryStatus{});
    }

    std::error_code ec;
    const fs::path relativeDirectory = fs::relative(directory, workdir, ec);
    if (ec)
    {
        return Result<GitDirectoryStatus>::success(GitDirectoryStatus{});
    }
    const std::string relativeDirectoryUtf8 = relativeDirectory == fs::path(".") ? std::string() : toGitRelativeUtf8(relativeDirectory);

    auto statusResult = repository.statusForImmediateChildren(relativeDirectoryUtf8);
    if (!statusResult)
    {
        return Result<GitDirectoryStatus>::failure(std::move(statusResult).error());
    }

    GitDirectoryStatus result;
    result.repositoryRoot = workdir;

    for (const LibGit2Repository::StatusEntry& entry : statusResult.value())
    {
        std::string_view remaining = entry.relativePathUtf8;
        if (!relativeDirectoryUtf8.empty())
        {
            const bool underDirectory = remaining.size() > relativeDirectoryUtf8.size() &&
                                         remaining.substr(0, relativeDirectoryUtf8.size()) == relativeDirectoryUtf8 &&
                                         remaining[relativeDirectoryUtf8.size()] == '/';
            if (!underDirectory)
            {
                continue;
            }
            remaining = remaining.substr(relativeDirectoryUtf8.size() + 1);
        }

        // libgit2 reports an untracked/ignored directory with a trailing slash on its path.
        if (!remaining.empty() && remaining.back() == '/')
        {
            remaining = remaining.substr(0, remaining.size() - 1);
        }

        if (remaining.empty())
        {
            continue;
        }

        // An entry nested further inside a subfolder of `directory` rolls up onto that
        // subfolder's own row (its name is `remaining`'s first path segment) instead of being
        // dropped, so a change several levels deep is still visible without opening every
        // intermediate folder.
        const std::size_t slashPos = remaining.find('/');
        const std::string_view childName = slashPos == std::string_view::npos ? remaining : remaining.substr(0, slashPos);

        // Ignored only rolls up onto the immediate child it was reported for (slashPos ==
        // npos), which is either an ignored file itself or a wholly-ignored subfolder -- both
        // genuinely ignored. For an entry nested *inside* a subfolder (slashPos != npos),
        // Ignored means some file further down happens to be gitignored while the subfolder
        // itself is still tracked/untracked; rolling that up would wrongly paint the whole
        // subfolder as ignored, so it's dropped instead of recorded.
        if (slashPos != std::string_view::npos && entry.status == GitFileStatus::Ignored)
        {
            continue;
        }

        recordStatus(result.statusByPath, directory / fromGitUtf8(childName), entry.status);
    }

    return Result<GitDirectoryStatus>::success(std::move(result));
}

Result<std::vector<fs::path>> LibGit2StatusProvider::ignoredPaths(const std::filesystem::path& root) const
{
    auto repoResult = LibGit2Repository::open(root);
    if (!repoResult)
    {
        // No repository at or above `root` -- tolerated by blanking (same posture as
        // statusForDirectory), not an error.
        return Result<std::vector<fs::path>>::success({});
    }

    LibGit2Repository repository = std::move(repoResult).value();
    const fs::path workdir = repository.workdirPath();
    if (workdir.empty())
    {
        // Bare repository -- no working tree, nothing to ignore.
        return Result<std::vector<fs::path>>::success({});
    }

    std::error_code ec;
    const fs::path relativeRoot = fs::relative(root, workdir, ec);
    if (ec)
    {
        return Result<std::vector<fs::path>>::success({});
    }
    const std::string relativeRootUtf8 = relativeRoot == fs::path(".") ? std::string() : toGitRelativeUtf8(relativeRoot);

    auto ignoredResult = repository.ignoredPaths(relativeRootUtf8);
    if (!ignoredResult)
    {
        return Result<std::vector<fs::path>>::failure(std::move(ignoredResult).error());
    }

    std::vector<fs::path> result;
    result.reserve(ignoredResult.value().size());
    for (const std::string& relativePathUtf8 : ignoredResult.value())
    {
        std::string_view trimmed = relativePathUtf8;
        // libgit2 reports a wholly-ignored directory with a trailing slash on its path.
        if (!trimmed.empty() && trimmed.back() == '/')
        {
            trimmed = trimmed.substr(0, trimmed.size() - 1);
        }

        result.push_back(workdir / fromGitUtf8(trimmed));
    }

    return Result<std::vector<fs::path>>::success(std::move(result));
}
