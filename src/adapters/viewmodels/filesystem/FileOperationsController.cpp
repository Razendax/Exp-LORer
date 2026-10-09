#include "FileOperationsController.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QUrl>

#include "FileNavigationUseCase.h"
#include "FileSizeFormat.h"
#include "FileTransferService.h"
#include "FileTransferTypes.h"
#include "NotificationCenter.h"
#include "PathContainment.h"

namespace
{
    constexpr quint32 kDropEffectCopy = 1;
    constexpr quint32 kDropEffectMove = 2;
    constexpr char kPreferredDropEffectFormat[] = "Preferred DropEffect";

    QByteArray dropEffectBytes(quint32 effect)
    {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << effect;
        return bytes;
    }

    quint32 readDropEffect(const QMimeData& mimeData)
    {
        const QString format = QString::fromLatin1(kPreferredDropEffectFormat);
        if (!mimeData.hasFormat(format))
        {
            return 0;
        }

        QByteArray bytes = mimeData.data(format);
        QDataStream stream(&bytes, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        quint32 effect = 0;
        stream >> effect;
        return effect;
    }

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // NotificationCenter::updateProgressBar/ProgressNotificationWidget::updateBar take `int`
    // current/max, matching QProgressBar's own int-only range API. Passing a raw byte count
    // through directly overflows for any file >= 2 GiB (INT_MAX), wrapping into a negative
    // `max`; QProgressBarPrivate::setRange then clamps maximum to std::max(minimum, maximum),
    // collapsing the range to min == max == 0 -- which Qt renders as a permanent busy/
    // indeterminate animation instead of real progress. Normalizing to a fixed scale keeps the
    // bar accurate regardless of file size.
    constexpr int kProgressScale = 10000;

    int scaledProgress(std::uintmax_t bytesSoFar, std::uintmax_t totalBytes)
    {
        if (totalBytes == 0)
        {
            return 0;
        }
        return static_cast<int>((bytesSoFar * kProgressScale) / totalBytes);
    }
}

FileOperationsController::FileOperationsController(FileNavigationUseCase& fileNavigationUseCase,
                                                     FileTransferService& fileTransferService,
                                                     NotificationCenter& notificationCenter, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileTransferService(fileTransferService)
    , m_notificationCenter(notificationCenter)
{
    // The Cancel button on a transfer's progress notification only emits
    // NotificationCenter::progressCancelRequested(id) -- nothing flips FileTransferService's actual
    // cancel flag unless something connects the two. pasteInto() is the only place that creates a
    // progress notification id, and it always passes that same id straight through to
    // FileTransferService::requestTransfer as the request id, so forwarding it here is correct for
    // every transfer this controller starts.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this,
            [this](NotificationCenter::ProgressId id) { m_fileTransferService.cancel(id); });
}

namespace
{
    QList<QUrl> toUrls(const std::vector<std::filesystem::path>& sources)
    {
        QList<QUrl> urls;
        urls.reserve(static_cast<qsizetype>(sources.size()));
        for (const auto& source : sources)
        {
            urls.append(QUrl::fromLocalFile(QString::fromStdWString(source.wstring())));
        }
        return urls;
    }
}

void FileOperationsController::copyToClipboard(const std::vector<std::filesystem::path>& sources)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls(toUrls(sources));
    mimeData->setData(QString::fromLatin1(kPreferredDropEffectFormat), dropEffectBytes(kDropEffectCopy));
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileOperationsController::cutToClipboard(const std::vector<std::filesystem::path>& sources)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls(toUrls(sources));
    mimeData->setData(QString::fromLatin1(kPreferredDropEffectFormat), dropEffectBytes(kDropEffectMove));
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileOperationsController::pasteInto(const std::filesystem::path& destinationDirectory)
{
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasUrls())
    {
        return;
    }

    const bool isMove = (readDropEffect(*mimeData) & kDropEffectMove) != 0;

    std::vector<std::filesystem::path> sourceParents;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> needsCopy;

    for (const QUrl& url : mimeData->urls())
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        const std::filesystem::path source(url.toLocalFile().toStdWString());

        // Pasting a cut item back into the very folder it was cut from is a no-op (matches
        // Explorer): without this check, uniqueDestinationName sees the item colliding with
        // itself and "moves" it sideways to e.g. "name (2).ext" instead of leaving it alone. A
        // copy-paste into the same folder is unaffected -- producing a "(2)" duplicate there is
        // the correct, intentional behavior.
        if (isMove && pathsEqualCaseInsensitive(source.parent_path(), destinationDirectory))
        {
            sourceParents.push_back(source.parent_path());
            continue;
        }

        const std::filesystem::path candidate = uniqueDestinationName(destinationDirectory, source.filename());

        if (isMove)
        {
            auto renamed = m_fileNavigationUseCase.moveFile(source, candidate);
            if (renamed.hasValue())
            {
                sourceParents.push_back(source.parent_path());
                continue;
            }
            // Any failure here (classically cross-volume) falls back to the copy+delete-source
            // pipeline below for this item; if the real problem is something else (e.g.
            // permissions), the subsequent copy attempt surfaces the same underlying error through
            // the batch's operationFailed message.
        }

        needsCopy.emplace_back(source, candidate);
        if (isMove)
        {
            // A copy-paste never touches its source, but a move-via-copy deletes it on success --
            // the source's parent needs refreshing either way once the batch finishes.
            sourceParents.push_back(source.parent_path());
        }
    }

    if (needsCopy.empty())
    {
        emit directoryContentsMayHaveChanged(destinationDirectory);

        std::sort(sourceParents.begin(), sourceParents.end());
        sourceParents.erase(std::unique(sourceParents.begin(), sourceParents.end()), sourceParents.end());
        for (const auto& parent : sourceParents)
        {
            emit directoryContentsMayHaveChanged(parent);
        }

        if (isMove)
        {
            QApplication::clipboard()->clear();
        }
        return;
    }

    std::sort(sourceParents.begin(), sourceParents.end());
    sourceParents.erase(std::unique(sourceParents.begin(), sourceParents.end()), sourceParents.end());

    // Cached above/below label text plus running file/byte counters, so the very frequent
    // onBytesProgress tick only needs to reformat the progress-count label (via progressText
    // below) instead of every label on the card.
    struct TransferDisplayState
    {
        std::size_t totalFiles = 0;
        std::uintmax_t totalBytes = 0;
        std::size_t filesStarted = 0;       // current (1-based) file number; incremented in onFileStarted.
        std::uintmax_t bytesCopiedSoFar = 0; // last value reported by onBytesProgress, across the whole plan.
        QString aboveText;
        QString belowText;
    };
    auto state = std::make_shared<TransferDisplayState>();

    // "File N of M -- A of B copied", refreshed on every onFileStarted/onBytesProgress tick so it
    // always reflects the latest counts without the caller re-deriving them.
    auto progressText = [](const TransferDisplayState& s) {
        return tr("File %1 of %2 -- %3 of %4 copied")
            .arg(static_cast<qulonglong>(std::min(s.filesStarted, s.totalFiles)))
            .arg(static_cast<qulonglong>(s.totalFiles))
            .arg(FileSizeFormat::format(s.bytesCopiedSoFar))
            .arg(FileSizeFormat::format(s.totalBytes));
    };

    const auto progressId =
        m_notificationCenter.createProgressNotification(isMove ? tr("Moving...") : tr("Copying..."), /*barCount=*/1,
                                                          /*cancellable=*/true);

    FileTransferService::Callbacks callbacks;

    callbacks.onEstimateProgress = [this, progressId, state](std::size_t filesSoFar, std::uintmax_t bytesSoFar) {
        state->totalFiles = filesSoFar;
        state->totalBytes = bytesSoFar;
        m_notificationCenter.updateProgressBar(
            progressId, 0, 0, 0,
            tr("Estimating: files %1, size %2").arg(static_cast<qulonglong>(filesSoFar)).arg(FileSizeFormat::format(bytesSoFar)),
            QString(), QString());
    };

    callbacks.onInsufficientSpace = [this, progressId](std::uintmax_t requiredBytes, std::uintmax_t availableBytes) {
        m_notificationCenter.terminateProgressNotification(progressId);
        emit insufficientDiskSpace(tr("Not enough free space: need %1, only %2 available.")
                                        .arg(FileSizeFormat::format(requiredBytes))
                                        .arg(FileSizeFormat::format(availableBytes)));
    };

    callbacks.onFileStarted = [this, progressId, isMove, state, needsCopy, progressText](const FileTransferItem& item) {
        if (state->aboveText.isEmpty())
        {
            state->aboveText = tr("%1 %2 file(s) (%3)")
                                    .arg(isMove ? tr("Moving") : tr("Copying"))
                                    .arg(static_cast<qulonglong>(state->totalFiles))
                                    .arg(FileSizeFormat::format(state->totalBytes));
        }

        state->filesStarted += 1;

        std::filesystem::path relative = item.source.filename();
        for (const auto& [rootSource, rootDestination] : needsCopy)
        {
            (void)rootDestination;
            if (isPathWithinOrEqual(item.source, rootSource))
            {
                relative = item.source.lexically_relative(rootSource.parent_path());
                break;
            }
        }

        state->belowText = tr("%1 (%2)").arg(toQString(relative)).arg(FileSizeFormat::format(item.size));
        m_notificationCenter.updateProgressBar(progressId, 0, 0, kProgressScale, state->aboveText, progressText(*state),
                                                state->belowText);
    };

    callbacks.onBytesProgress = [this, progressId, state, progressText](std::uintmax_t bytesCopiedSoFar, std::uintmax_t totalBytes) {
        state->bytesCopiedSoFar = bytesCopiedSoFar;
        m_notificationCenter.updateProgressBar(progressId, 0, scaledProgress(bytesCopiedSoFar, totalBytes), kProgressScale,
                                                state->aboveText, progressText(*state), state->belowText);
    };

    callbacks.onFinished = [this, progressId, isMove, destinationDirectory, sourceParents](Result<void> result) {
        m_notificationCenter.terminateProgressNotification(progressId);

        // Still refresh even on cancel/failure -- partial progress may have happened already.
        emit directoryContentsMayHaveChanged(destinationDirectory);
        for (const auto& parent : sourceParents)
        {
            emit directoryContentsMayHaveChanged(parent);
        }

        if (isMove)
        {
            QApplication::clipboard()->clear();
        }

        if (result.hasError())
        {
            if (result.error().code != ErrorCode::Cancelled)
            {
                emit operationFailed(QString::fromStdString(result.error().message));
            }
            return;
        }

        m_notificationCenter.showPopup(NotificationType::Info, isMove ? tr("Move Complete") : tr("Copy Complete"),
                                        isMove ? tr("Items moved into %1.").arg(toQString(destinationDirectory))
                                               : tr("Items copied into %1.").arg(toQString(destinationDirectory)));
    };

    m_fileTransferService.requestTransfer(this, progressId, needsCopy, isMove, callbacks);
}

void FileOperationsController::moveToTrash(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.moveFileToTrash(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit pathRemoved(path);
    emit directoryContentsMayHaveChanged(path.parent_path());
}

void FileOperationsController::deletePermanently(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.deleteFilePermanently(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit pathRemoved(path);
    emit directoryContentsMayHaveChanged(path.parent_path());
}

void FileOperationsController::openFile(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.openFile(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
    }
}

void FileOperationsController::extractArchive(const std::filesystem::path& archiveFile,
                                                const std::filesystem::path& destinationDirectory)
{
    auto result = m_fileNavigationUseCase.extractArchive(archiveFile, destinationDirectory);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit directoryContentsMayHaveChanged(destinationDirectory);
}

void FileOperationsController::renamePath(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    emit pathAboutToBeRenamed(source, destination);

    auto result = m_fileNavigationUseCase.moveFile(source, destination);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    // pathRenamed first: WorkspaceController renames the folder tree's node in place before the
    // parent's re-list diff runs, so an expanded subtree survives (Architecture.md §14.41).
    emit pathRenamed(source, destination);
    emit directoryContentsMayHaveChanged(source.parent_path());
}

void FileOperationsController::bulkRename(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& renames)
{
    if (renames.empty())
    {
        return;
    }

    std::size_t failureCount = 0;
    QString firstFailureMessage;
    std::filesystem::path sharedParent;

    for (const auto& [source, destination] : renames)
    {
        emit pathAboutToBeRenamed(source, destination);

        auto result = m_fileNavigationUseCase.moveFile(source, destination);
        if (result.hasError())
        {
            if (failureCount == 0)
            {
                firstFailureMessage = QString::fromStdString(result.error().message);
            }
            ++failureCount;
            continue;
        }

        // pathRenamed first: WorkspaceController renames the folder tree's node in place before the
        // parent's re-list diff runs, so an expanded subtree survives (Architecture.md §14.41) --
        // same ordering renamePath() uses.
        emit pathRenamed(source, destination);
        sharedParent = source.parent_path();
    }

    if (!sharedParent.empty())
    {
        emit directoryContentsMayHaveChanged(sharedParent);
    }

    if (failureCount > 0)
    {
        emit operationFailed(tr("%1 item(s) could not be renamed: %2").arg(failureCount).arg(firstFailureMessage));
    }
}

Result<std::vector<ContextMenuEntry>> FileOperationsController::buildContextMenuForSelection(
    const std::vector<std::filesystem::path>& paths, ContextMenuSourceMode mode)
{
    return m_fileNavigationUseCase.buildItemContextMenu(paths, mode);
}

Result<std::vector<ContextMenuEntry>> FileOperationsController::buildContextMenuForFolder(const std::filesystem::path& folder,
                                                                                            ContextMenuSourceMode mode)
{
    return m_fileNavigationUseCase.buildBackgroundContextMenu(folder, mode);
}

void FileOperationsController::invokeContextMenuEntry(std::uint32_t entryId, const std::filesystem::path& directory,
                                                        NativeWindowHandle ownerWindow)
{
    auto result = m_fileNavigationUseCase.invokeContextMenuEntry(entryId, ownerWindow);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    // The invoked entry may be registry/COM-sourced, so we can't know what it did to disk —
    // refresh-on-any-success is the simple correct default, same posture as paste/delete.
    emit directoryContentsMayHaveChanged(directory);
}

void FileOperationsController::discardContextMenu()
{
    m_fileNavigationUseCase.discardContextMenu();
}

std::optional<std::filesystem::path> FileOperationsController::createFolder(const std::filesystem::path& parentDirectory)
{
    const std::filesystem::path candidate = uniqueDestinationName(parentDirectory, "New folder");
    auto result = m_fileNavigationUseCase.createFolder(candidate);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return std::nullopt;
    }

    emit directoryContentsMayHaveChanged(parentDirectory);
    return candidate;
}

void FileOperationsController::createFileFromTemplate(const std::filesystem::path& destinationFile,
                                                        const std::optional<std::filesystem::path>& templateFile)
{
    auto result = m_fileNavigationUseCase.createFileFromTemplate(destinationFile, templateFile);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit directoryContentsMayHaveChanged(destinationFile.parent_path());
}

void FileOperationsController::showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow)
{
    auto result = m_fileNavigationUseCase.showProperties(path, ownerWindow);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
    }
}

std::filesystem::path FileOperationsController::uniqueDestinationName(const std::filesystem::path& destinationDirectory,
                                                                       const std::filesystem::path& desiredName) const
{
    const std::filesystem::path candidate = destinationDirectory / desiredName;
    if (m_fileNavigationUseCase.stat(candidate).hasError())
    {
        return candidate;
    }

    const std::filesystem::path stem = desiredName.stem();
    const std::filesystem::path extension = desiredName.extension();

    for (int counter = 2;; ++counter)
    {
        std::filesystem::path attemptName = stem;
        attemptName += L" (" + std::to_wstring(counter) + L")";
        attemptName += extension;

        const std::filesystem::path attempt = destinationDirectory / attemptName;
        if (m_fileNavigationUseCase.stat(attempt).hasError())
        {
            return attempt;
        }
    }
}
