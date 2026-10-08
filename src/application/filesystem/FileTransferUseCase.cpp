#include "FileTransferUseCase.h"

#include "PathContainment.h"

namespace fs = std::filesystem;

FileTransferUseCase::FileTransferUseCase(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<FileTransferPlan> FileTransferUseCase::planTransfer(
    const std::vector<std::pair<fs::path, fs::path>>& sourcesToDestinationRoots,
    const std::function<void(std::size_t filesSoFar, std::uintmax_t bytesSoFar)>& onCountProgress,
    const std::function<bool()>& shouldCancel)
{
    FileTransferPlan plan;
    std::size_t baseFileCount = 0;
    std::uintmax_t baseByteCount = 0;

    for (const auto& [source, destinationRoot] : sourcesToDestinationRoots)
    {
        if (shouldCancel && shouldCancel())
        {
            break;
        }

        auto statResult = m_fileSystemRepository.stat(source);
        if (statResult.hasError())
        {
            return Result<FileTransferPlan>::failure(std::move(statResult).error());
        }

        const FileNode& node = statResult.value();
        if (!node.isDirectory())
        {
            plan.items.push_back(FileTransferItem{ source, destinationRoot, false, node.size() });
            plan.totalFileCount += 1;
            plan.totalBytes += node.size();
            baseFileCount += 1;
            baseByteCount += node.size();

            if (onCountProgress)
            {
                onCountProgress(baseFileCount, baseByteCount);
            }
            continue;
        }

        plan.items.push_back(FileTransferItem{ source, destinationRoot, true, 0 });

        auto onProgress = [&](const std::vector<FileNode>& partial) {
            if (!onCountProgress)
            {
                return;
            }

            std::size_t files = 0;
            std::uintmax_t bytes = 0;
            for (const auto& entry : partial)
            {
                if (!entry.isDirectory())
                {
                    files += 1;
                    bytes += entry.size();
                }
            }
            onCountProgress(baseFileCount + files, baseByteCount + bytes);
        };

        auto listResult = m_fileSystemRepository.listDirectoryRecursive(source, onProgress, nullptr);
        if (listResult.hasError())
        {
            return Result<FileTransferPlan>::failure(std::move(listResult).error());
        }

        std::size_t filesInRoot = 0;
        std::uintmax_t bytesInRoot = 0;
        for (const auto& descendant : listResult.value())
        {
            const fs::path relative = descendant.path().lexically_relative(source);
            const fs::path mappedDestination = destinationRoot / relative;
            const bool isDirectory = descendant.isDirectory();
            const std::uintmax_t size = isDirectory ? 0 : descendant.size();

            plan.items.push_back(FileTransferItem{ descendant.path(), mappedDestination, isDirectory, size });
            if (!isDirectory)
            {
                plan.totalFileCount += 1;
                plan.totalBytes += size;
                filesInRoot += 1;
                bytesInRoot += size;
            }
        }

        baseFileCount += filesInRoot;
        baseByteCount += bytesInRoot;
        if (onCountProgress)
        {
            onCountProgress(baseFileCount, baseByteCount);
        }
    }

    return Result<FileTransferPlan>::success(std::move(plan));
}

Result<void> FileTransferUseCase::checkDestinationHasSpace(const fs::path& destinationDirectory, std::uintmax_t requiredBytes)
{
    auto driveInfo = m_fileSystemRepository.driveSpaceInfo(destinationDirectory);
    if (driveInfo.hasError())
    {
        // Can't verify (virtual path, unreachable volume) -- same tolerant posture as the status
        // bar's own use of driveSpaceInfo: allow the transfer rather than blocking it.
        return Result<void>::success();
    }

    if (driveInfo.value().freeBytes < requiredBytes)
    {
        return Result<void>::failure(Error(ErrorCode::InsufficientDiskSpace, "Not enough free space on the destination drive"));
    }

    return Result<void>::success();
}

Result<void> FileTransferUseCase::executeTransfer(const FileTransferPlan& plan,
                                                   const std::vector<std::pair<fs::path, fs::path>>& topLevelRoots,
                                                   bool isMove, const FileTransferCallbacks& callbacks)
{
    std::uintmax_t bytesCopiedSoFar = 0;

    for (const auto& [rootSource, rootDestination] : topLevelRoots)
    {
        (void)rootDestination;

        for (const auto& item : plan.items)
        {
            if (!isPathWithinOrEqual(item.source, rootSource))
            {
                continue;
            }

            if (callbacks.shouldCancel && callbacks.shouldCancel())
            {
                return Result<void>::failure(Error(ErrorCode::Cancelled, "Transfer cancelled"));
            }

            if (item.isDirectory)
            {
                auto dirResult = m_fileSystemRepository.createDirectory(item.destination);
                if (dirResult.hasError() && dirResult.error().code != ErrorCode::AlreadyExists)
                {
                    return Result<void>::failure(std::move(dirResult).error());
                }
                continue;
            }

            if (callbacks.onFileStarted)
            {
                callbacks.onFileStarted(item);
            }

            const std::uintmax_t bytesBeforeThisFile = bytesCopiedSoFar;
            auto onChunk = [&](std::uint64_t chunkBytes) -> bool {
                if (callbacks.shouldCancel && callbacks.shouldCancel())
                {
                    return false;
                }
                if (callbacks.onBytesProgress)
                {
                    callbacks.onBytesProgress(bytesBeforeThisFile + chunkBytes);
                }
                return true;
            };

            auto copyResult = m_fileSystemRepository.copyFileWithProgress(item.source, item.destination, onChunk);
            if (copyResult.hasError())
            {
                return Result<void>::failure(std::move(copyResult).error());
            }

            bytesCopiedSoFar += item.size;
            if (callbacks.onBytesProgress)
            {
                callbacks.onBytesProgress(bytesCopiedSoFar);
            }
        }

        // Every item belonging to this top-level root copied successfully (any failure/cancel
        // above returns before reaching here) -- safe to remove the original now for a move.
        // Already-completed earlier roots keep their deletion even if a later root is cancelled.
        if (isMove)
        {
            m_fileSystemRepository.deletePermanently(rootSource);
        }
    }

    return Result<void>::success();
}
