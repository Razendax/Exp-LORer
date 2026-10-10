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
