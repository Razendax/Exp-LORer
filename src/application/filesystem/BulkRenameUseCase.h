#pragma once

#include <filesystem>
#include <vector>

#include "BulkRenamePattern.h"
#include "FileNode.h"
#include "Result.h"

class IFileSystemRepository;

// Computes the before/after preview for the Bulk Rename tool (Architecture.md §14.55). Stateless,
// depends only on IFileSystemRepository -- same "never call another use case" posture as
// FileTransferUseCase/FilePreviewUseCase. Has no execute(): applying the renames reuses
// FileOperationsController::bulkRename so the existing per-rename tab/folder-tree repoint machinery
// (§14.41) keeps firing unchanged.
class BulkRenameUseCase
{
public:
    explicit BulkRenameUseCase(IFileSystemRepository& fileSystemRepository);

    enum class EntryStatus
    {
        Ok,
        Unchanged,
        CollisionWithExisting,
        CollisionWithinBatch,
        InvalidName,
    };

    struct PreviewEntry
    {
        std::filesystem::path source;
        std::filesystem::path proposedPath; // same parent directory, new filename
        EntryStatus status;
    };

    // All of `selection` must share one parent directory (true for every current caller: a
    // multi-selection or a folder listing both come from one tab's current folder). A bad regex in
    // `pattern` fails the whole preview (Result::failure), same granularity RegexMatch uses, since
    // it invalidates every entry at once.
    Result<std::vector<PreviewEntry>> computePreview(const std::vector<FileNode>& selection,
                                                       const BulkRenamePattern& pattern) const;

private:
    IFileSystemRepository& m_fileSystemRepository;
};
