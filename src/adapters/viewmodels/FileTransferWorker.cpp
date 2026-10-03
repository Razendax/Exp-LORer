#include "FileTransferWorker.h"

#include "IFileSystemRepository.h"

FileTransferWorker::FileTransferWorker(IFileSystemRepository& fileSystemRepository, QObject* parent)
    : QObject(parent)
    , m_fileSystemRepository(fileSystemRepository)
    , m_fileTransferUseCase(fileSystemRepository)
{
}

Result<void> FileTransferWorker::runTransfer(
    const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& topLevelRoots, bool isMove,
    const std::shared_ptr<std::atomic<bool>>& cancelFlag,
    const std::function<void(std::size_t filesSoFar, std::uintmax_t bytesSoFar)>& onEstimateProgress,
    const std::function<void(std::uintmax_t requiredBytes, std::uintmax_t availableBytes)>& onInsufficientSpace,
    const std::function<void(const FileTransferItem& item)>& onFileStarted,
    const std::function<void(std::uintmax_t bytesCopiedSoFar, std::uintmax_t totalBytes)>& onBytesProgress)
{
    auto shouldCancel = [cancelFlag]() { return cancelFlag->load(std::memory_order_relaxed); };

    auto planResult = m_fileTransferUseCase.planTransfer(topLevelRoots, onEstimateProgress, shouldCancel);
    if (planResult.hasError())
    {
        return Result<void>::failure(std::move(planResult).error());
    }

    const FileTransferPlan& plan = planResult.value();

    if (shouldCancel())
    {
        return Result<void>::failure(Error(ErrorCode::Cancelled, "Transfer cancelled"));
    }

    if (!topLevelRoots.empty())
    {
        const std::filesystem::path destinationDirectory = topLevelRoots.front().second.parent_path();
        auto spaceResult = m_fileTransferUseCase.checkDestinationHasSpace(destinationDirectory, plan.totalBytes);
        if (spaceResult.hasError())
        {
            std::uintmax_t availableBytes = 0;
            auto driveInfo = m_fileSystemRepository.driveSpaceInfo(destinationDirectory);
            if (driveInfo.hasValue())
            {
                availableBytes = driveInfo.value().freeBytes;
            }

            if (onInsufficientSpace)
            {
                onInsufficientSpace(plan.totalBytes, availableBytes);
            }
            return Result<void>::failure(std::move(spaceResult).error());
        }
    }

    FileTransferCallbacks callbacks;
    callbacks.onFileStarted = onFileStarted;
    callbacks.onBytesProgress = [onBytesProgress, totalBytes = plan.totalBytes](std::uintmax_t bytesSoFar) {
        if (onBytesProgress)
        {
            onBytesProgress(bytesSoFar, totalBytes);
        }
    };
    callbacks.shouldCancel = shouldCancel;

    return m_fileTransferUseCase.executeTransfer(plan, topLevelRoots, isMove, callbacks);
}
