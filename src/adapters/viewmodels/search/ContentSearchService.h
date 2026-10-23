#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <QObject>

#include "ContentSearchMatch.h"
#include "ContentSearchOptions.h"
#include "FileNode.h"
#include "Result.h"
#include "WorkerThreadService.h"

class ContentSearchUseCase;
class ContentSearchWorker;
class FileNavigationUseCase;
class IFileSystemRepository;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown --
// same lifetime posture as AdvancedSearchService/GitStatusService). Runs "Find in Files"'s
// (Architecture.md §14.104) recursive-scan requests on one dedicated background QThread, off the
// Qt UI thread.
//
// Context-guarded/callback API, same shape as AdvancedSearchService -- a TabContentSearchViewModel
// instance can be destroyed (tab closed) while a scan is still in flight.
class ContentSearchService : public WorkerThreadService
{
    Q_OBJECT

public:
    ContentSearchService(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                          ContentSearchUseCase& contentSearchUseCase, QObject* parent = nullptr);

    // onProgress, when set, is invoked on `context`'s thread with the files discovered so far,
    // roughly once/second while the scan runs (StandardFileSystemRepository's throttling) --
    // letting the results populate progressively instead of only once the whole recursive walk
    // finishes. Discarded the same way as callback if `context` is destroyed mid-scan.
    void requestScan(QObject* context, const std::filesystem::path& root, const std::string& includePattern,
                      const TextMatchOptions& includeOptions, const std::string& excludePattern,
                      const TextMatchOptions& excludeOptions, bool skipGitIgnored, bool showHidden,
                      std::function<void(Result<std::vector<RawFileLines>>)> callback,
                      std::function<void(std::vector<FileNode>)> onProgress = nullptr);

private:
    ContentSearchWorker* m_worker = nullptr;
};
