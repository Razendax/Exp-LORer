#pragma once

#include <cstddef>

#include "FilePreview.h"
#include "FileNode.h"
#include "Result.h"

class IFileSystemRepository;
class IMediaDecoder;

// Classifies a target as folder/text/image/video/unsupported and builds the FilePreview for the
// right panel's Preview tab (Architecture.md §14.25). Stateless, depends directly on Ports (never
// on another use case) so it stays mockable in isolation (§11).
class FilePreviewUseCase
{
public:
    FilePreviewUseCase(IFileSystemRepository& fileSystemRepository, IMediaDecoder& mediaDecoder);

    Result<FilePreview> generatePreview(const FileNode& target, int maxImageWidth, int maxImageHeight,
                                         std::size_t maxTextBytes) const;

private:
    IFileSystemRepository& m_fileSystemRepository;
    IMediaDecoder& m_mediaDecoder;
};
