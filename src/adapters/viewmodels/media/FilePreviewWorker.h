#pragma once

#include <cstddef>

#include <QObject>

#include "FileNode.h"
#include "FilePreview.h"
#include "Result.h"

class FilePreviewUseCase;

// Runs FilePreviewUseCase::generatePreview on whatever thread it has been moved to
// (Architecture.md §5/§14.25). Owned and driven by FilePreviewViewModel, which dispatches to and
// receives from this worker via QMetaObject::invokeMethod(..., Qt::QueuedConnection) rather than
// Qt signals/slots -- FileNode/FilePreview/Result aren't default-constructible, which would make
// Qt meta-type registration for cross-thread signal/slot delivery an unnecessary risk when a
// plain queued functor call does the same job with zero registration.
class FilePreviewWorker : public QObject
{
    Q_OBJECT

public:
    explicit FilePreviewWorker(FilePreviewUseCase& useCase, QObject* parent = nullptr);

    Result<FilePreview> generate(const FileNode& target, int maxImageWidth, int maxImageHeight,
                                  std::size_t maxTextBytes) const;

private:
    FilePreviewUseCase& m_useCase;
};
