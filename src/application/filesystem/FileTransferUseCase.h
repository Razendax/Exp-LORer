#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

#include "FileTransferTypes.h"
#include "IFileSystemRepository.h"
#include "Result.h"

// Plans and executes a multi-file copy or move with per-file progress and cooperative
// cancellation, backing the custom copy/move progress notification (Architecture.md §14.47).
// Stateless, depends only on IFileSystemRepository -- same "never call another use case" posture
// as FilePreviewUseCase.
class FileTransferUseCase
{
public:
    explicit FileTransferUseCase(IFileSystemRepository& fileSystemRepository);

    // Walks every (source, destinationRoot) pair that needs real byte-copying and builds the flat
    // plan. For a source that's a plain file, one stat() call. For a directory, stat() the root +
    // IFileSystemRepository::listDirectoryRecursive under it, remapping every descendant's path
    // from under `source` to under `destinationRoot` via lexically_relative.
    //
    // onCountProgress(filesSoFar, bytesSoFar) is forwarded from listDirectoryRecursive's own
    // ~1/sec throttling, accumulating a running base offset across multiple top-level roots so the
    // numbers are a grand total, not per-root. shouldCancel is polled between top-level roots --
    // forwarding it into listDirectoryRecursive's own loop isn't possible (that Port method has no
    // cancel hook): estimating a single huge subtree can't be interrupted mid-walk, same documented
    // limitation as Advanced Search's scan (Architecture.md §5). A cancel request during
    // *estimating* takes effect at the next top-level root boundary, and always takes full effect
    // once the copy phase starts.
    Result<FileTransferPlan> planTransfer(
        const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& sourcesToDestinationRoots,
        const std::function<void(std::size_t filesSoFar, std::uintmax_t bytesSoFar)>& onCountProgress,
        const std::function<bool()>& shouldCancel);

    // Wraps IFileSystemRepository::driveSpaceInfo(destinationDirectory); failure to resolve drive
    // info is treated as "can't verify, allow it" (same tolerant posture as the status bar's own
    // use of driveSpaceInfo) rather than blocking the operation.
    Result<void> checkDestinationHasSpace(const std::filesystem::path& destinationDirectory, std::uintmax_t requiredBytes);

    // Executes the plan: creates every directory entry (and every file's parent chain) at the
    // destination before copying into it, tolerant of AlreadyExists -- so plan ordering never
    // matters, directories don't need to precede their files in `plan.items`. For each file, calls
    // IFileSystemRepository::copyFileWithProgress, chunked, honoring shouldCancel.
    //
    // On full-plan success, if isMove, deletes each top-level source root (deletePermanently -- a
    // move never goes through the Recycle Bin) -- but only those whose every file finished copying.
    //
    // On shouldCancel()==true: stops before starting the next file, deletes the currently-
    // in-progress file's partial destination (if any bytes were written), returns
    // Error(ErrorCode::Cancelled, ...). Files already fully copied are NOT rolled back and (for a
    // move) their sources ARE deleted up to the point of cancellation -- Explorer-like "best effort
    // so far" semantics, not an all-or-nothing transaction.
    Result<void> executeTransfer(const FileTransferPlan& plan,
                                 const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& topLevelRoots,
                                 bool isMove, const FileTransferCallbacks& callbacks);

private:
    IFileSystemRepository& m_fileSystemRepository;
};
