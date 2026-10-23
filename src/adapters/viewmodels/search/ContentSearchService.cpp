#include "ContentSearchService.h"

#include <utility>

#include "ContentSearchWorker.h"
#include "Logging.h"

ContentSearchService::ContentSearchService(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository,
                                            ContentSearchUseCase& contentSearchUseCase, QObject* parent)
    : WorkerThreadService(parent)
{
    m_worker = new ContentSearchWorker(fileNavigationUseCase, fileSystemRepository, contentSearchUseCase);
    startWorker(m_worker);
}

void ContentSearchService::requestScan(QObject* context, const std::filesystem::path& root, const std::string& includePattern,
                                        const TextMatchOptions& includeOptions, const std::string& excludePattern,
                                        const TextMatchOptions& excludeOptions, bool skipGitIgnored, bool showHidden,
                                        std::function<void(Result<std::vector<RawFileLines>>)> callback,
                                        std::function<void(std::vector<FileNode>)> onProgress)
{
    QPointer<QObject> guard(context);

    runOnWorker([this, guard, root, includePattern, includeOptions, excludePattern, excludeOptions, skipGitIgnored, showHidden,
                 callback = std::move(callback), onProgress = std::move(onProgress)]() {
        auto progressBridge = [this, guard, onProgress](const std::vector<FileNode>& partial) {
            invokeGuarded(guard, onProgress, partial);
        };

        Result<std::vector<RawFileLines>> result =
            m_worker->scan(root, includePattern, includeOptions, excludePattern, excludeOptions, skipGitIgnored, showHidden,
                           progressBridge);

        if (result.hasError())
        {
            Logging::warn("ContentSearchService: scan failed: " + std::string(toLogString(result.error().code)));
        }
        else
        {
            Logging::debug("ContentSearchService: scan finished (" + std::to_string(result.value().size()) + " file(s))");
        }

        invokeGuarded(guard, callback, std::move(result));
    });
}
