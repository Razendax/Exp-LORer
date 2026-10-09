#include "MediaProcessingUseCase.h"

MediaProcessingUseCase::MediaProcessingUseCase(IMediaDecoder& mediaDecoder)
    : m_mediaDecoder(mediaDecoder)
{
}

Result<MediaThumbnail> MediaProcessingUseCase::generateThumbnail(const std::filesystem::path& mediaFile,
                                                                     int maxWidth,
                                                                     int maxHeight) const
{
    if (maxWidth <= 0 || maxHeight <= 0)
    {
        return Result<MediaThumbnail>::failure(
            Error(ErrorCode::InvalidArgument, "Thumbnail maxWidth/maxHeight must be positive"));
    }

    return m_mediaDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight);
}
