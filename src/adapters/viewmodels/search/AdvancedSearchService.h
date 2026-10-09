#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QObject>
#include <QThread>

#include "FileNode.h"
#include "Result.h"
#include "Tag.h"

class FileNavigationUseCase;
class IFileSystemRepository;
class AdvancedSearchWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as GitStatusService/FilePreviewViewModel). Runs the Advanced Search pane's
// recursive-scan and tag-first-candidate requests (Architecture.md §14.19/§14.22) on one dedicated
// background QThread, off the Qt UI thread.
//
// Uses a context-guarded/callback API (not FilePreviewViewModel's own-signals style) because,
// unlike the singleton FilePreviewViewModel, TabViewModel instances are destroyed when a tab
// closes while a request could still be in flight -- the QPointer<QObject> guard(context) pattern
// GitStatusService already uses is exactly the tool for that.
class AdvancedSearchService : public QObject
{
    Q_OBJECT

public:
    AdvancedSearchService(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                           const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~AdvancedSearchService() override;

    // onProgress, when set, is invoked on `context`'s thread with the files discovered so far,
    // roughly once/second while the scan runs (StandardFileSystemRepository's throttling,
    // Architecture.md §14.37) -- letting Advanced Search populate results progressively instead of
    // only once the whole recursive walk finishes. Discarded the same way as callback if `context`
    // is destroyed mid-scan.
    //
    // skipGitIgnored forwards to AdvancedSearchWorker::scanRecursive (Architecture.md §14.45);
    // defaults to false, preserving the Advanced Search pane's own call site unchanged.
    void requestRecursiveScan(QObject* context, const std::filesystem::path& root,
                               std::function<void(Result<std::vector<FileNode>>)> callback,
                               std::function<void(std::vector<FileNode>)> onProgress = nullptr, bool skipGitIgnored = false);

    void requestTagFirstCandidates(QObject* context, const std::filesystem::path& root, const std::vector<Tag::Id>& tagIds,
                                    std::function<void(Result<std::vector<FileNode>>)> callback);

private:
    QThread m_workerThread;
    AdvancedSearchWorker* m_worker = nullptr;
};
