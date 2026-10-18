#include "AdvancedSearchService.h"

#include <utility>

#include "AdvancedSearchWorker.h"
#include "Logging.h"

AdvancedSearchService::AdvancedSearchService(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                                              const std::filesystem::path& databasePath, QObject* parent)
    : WorkerThreadService(parent)
{
    m_worker = new AdvancedSearchWorker(fileNavigationUseCase, fileSystemRepository, databasePath);
    startWorker(m_worker);
}

void AdvancedSearchService::requestRecursiveScan(QObject* context, const std::filesystem::path& root,
                                                  std::function<void(Result<std::vector<FileNode>>)> callback,
                                                  std::function<void(std::vector<FileNode>)> onProgress, bool skipGitIgnored)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, root, callback = std::move(callback), onProgress = std::move(onProgress), skipGitIgnored]() {
        // Called synchronously on this worker thread from inside the scan below -- bounce each
        // tick back to the requesting side the same way the final result is, so onProgress never
        // runs on the wrong thread and never fires after `context` is gone.
        auto progressBridge = [this, guard, onProgress](const std::vector<FileNode>& partial) {
            invokeGuarded(guard, onProgress, partial);
        };

        Result<std::vector<FileNode>> result = m_worker->scanRecursive(root, progressBridge, skipGitIgnored);

        if (result.hasError())
        {
            Logging::warn("AdvancedSearchService: recursive scan failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("AdvancedSearchService: recursive scan finished (" + std::to_string(result.value().size()) +
                            " result(s))");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}

void AdvancedSearchService::requestTagFirstCandidates(QObject* context, const std::filesystem::path& root,
                                                       const std::vector<Tag::Id>& tagIds,
                                                       std::function<void(Result<std::vector<FileNode>>)> callback)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, root, tagIds, callback = std::move(callback)]() {
        Result<std::vector<FileNode>> result = m_worker->fetchTagFirstCandidates(root, tagIds);

        if (result.hasError())
        {
            Logging::warn("AdvancedSearchService: tag-first candidates failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("AdvancedSearchService: tag-first candidates finished (" + std::to_string(result.value().size()) +
                            " result(s))");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}

void AdvancedSearchService::requestTagFirstCandidatesIncludingChildren(
    QObject* context, const std::filesystem::path& root, const std::vector<Tag::Id>& tagIds,
    std::function<void(Result<std::vector<FileNode>>)> callback)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, root, tagIds, callback = std::move(callback)]() {
        Result<std::vector<FileNode>> result = m_worker->fetchTagFirstCandidatesIncludingChildren(root, tagIds);

        if (result.hasError())
        {
            Logging::warn("AdvancedSearchService: tag-first candidates (including children) failed: " +
                           std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("AdvancedSearchService: tag-first candidates (including children) finished (" +
                            std::to_string(result.value().size()) + " result(s))");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}

void AdvancedSearchService::requestFilesForTag(QObject* context, Tag::Id tagId,
                                                 std::function<void(Result<std::vector<FileNode>>)> callback)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, tagId, callback = std::move(callback)]() {
        Result<std::vector<FileNode>> result = m_worker->fetchFilesForTag(tagId);

        if (result.hasError())
        {
            Logging::warn("AdvancedSearchService: files-for-tag failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("AdvancedSearchService: files-for-tag finished (" + std::to_string(result.value().size()) +
                            " result(s))");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}
