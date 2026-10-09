#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QObject>

#include "FileTransferTypes.h"
#include "FileTransferUseCase.h"
#include "Result.h"

class IFileSystemRepository;

// Runs the I/O parts of the custom copy/move pipeline (Architecture.md §14.47) on whatever thread
// it has been moved to. Owned and driven by FileTransferService, same shape as
// AdvancedSearchWorker/GitStatusWorker. Owns one FileTransferUseCase, constructed from the shared
// IFileSystemRepository& -- that use case is stateless/Qt-free, so owning it here (rather than
// sharing one instance across threads) is just a convenience, not a thread-affinity requirement.
class FileTransferWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferWorker(IFileSystemRepository& fileSystemRepository, QObject* parent = nullptr);

    // Runs plan -> space-check -> execute end to end on this worker's own thread. The progress
    // callbacks are invoked synchronously on this thread; FileTransferService is responsible for
    // marshalling them back to the requesting thread, same posture as AdvancedSearchWorker's
    // scanRecursive onProgress. onInsufficientSpace additionally reports the destination's actual
    // free space (re-queried via IFileSystemRepository::driveSpaceInfo) for the dialog's message --
    // FileTransferUseCase::checkDestinationHasSpace itself stays a plain pass/fail Result<void>.
    Result<void> runTransfer(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& topLevelRoots,
                              bool isMove, const std::shared_ptr<std::atomic<bool>>& cancelFlag,
                              const std::function<void(std::size_t filesSoFar, std::uintmax_t bytesSoFar)>& onEstimateProgress,
                              const std::function<void(std::uintmax_t requiredBytes, std::uintmax_t availableBytes)>& onInsufficientSpace,
                              const std::function<void(const FileTransferItem& item)>& onFileStarted,
                              const std::function<void(std::uintmax_t bytesCopiedSoFar, std::uintmax_t totalBytes)>& onBytesProgress);

private:
    IFileSystemRepository& m_fileSystemRepository;
    FileTransferUseCase m_fileTransferUseCase;
};
