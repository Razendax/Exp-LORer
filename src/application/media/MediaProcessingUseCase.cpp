#include "MediaProcessingUseCase.h"

#include "MediaPreviewFrames.h"

MediaProcessingUseCase::MediaProcessingUseCase(IMediaDecoder& mediaDecoder)
    : m_mediaDecoder(mediaDecoder)
{
}

Result<MediaThumbnail> MediaProcessingUseCase::generateThumbnail(const std::filesystem::path& mediaFile,
                                                                     int maxWidth,
                                                                     int maxHeight,
                                                                     int previewIndex) const
{
    if (maxWidth <= 0 || maxHeight <= 0)
    {
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::InvalidArgument, "Thumbnail maxWidth/maxHeight must be positive"));
    }
    if (previewIndex < 0 || previewIndex >= MediaPreviewFrames::kCount)
    {
        return Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "previewIndex out of range"));
    }

    return m_mediaDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
}

std::vector<Result<MediaThumbnail>> MediaProcessingUseCase::generateThumbnails(const std::filesystem::path& mediaFile,
                                                                                  int maxWidth, int maxHeight,
                                                                                  const std::vector<int>& previewIndices) const
{
    if (maxWidth <= 0 || maxHeight <= 0)
    {
        return std::vector<Result<MediaThumbnail>>(
            previewIndices.size(),
            Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "Thumbnail maxWidth/maxHeight must be positive")));
    }

    for (int previewIndex : previewIndices)
    {
        if (previewIndex < 0 || previewIndex >= MediaPreviewFrames::kCount)
        {
            return std::vector<Result<MediaThumbnail>>(
                previewIndices.size(), Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "previewIndex out of range")));
        }
    }

    return m_mediaDecoder.generateThumbnails(mediaFile, maxWidth, maxHeight, previewIndices);
}
