#include "FilePreviewWorker.h"

#include "FilePreviewUseCase.h"

FilePreviewWorker::FilePreviewWorker(FilePreviewUseCase& useCase, QObject* parent)
    : QObject(parent)
    , m_useCase(useCase)
{
}

Result<FilePreview> FilePreviewWorker::generate(const FileNode& target, int maxImageWidth, int maxImageHeight,
                                                  std::size_t maxTextBytes) const
{
    return m_useCase.generatePreview(target, maxImageWidth, maxImageHeight, maxTextBytes);
}
