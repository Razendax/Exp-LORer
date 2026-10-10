#include "MediaDecoder.h"

#include "MediaExtensions.h"

Result<MediaThumbnail> MediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                          int maxHeight, int previewIndex) const
{
    const std::string extension = MediaExtensions::lowercaseExtension(mediaFile);

    if (MediaExtensions::isImageExtension(extension))
    {
        return m_imageDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }
    if (MediaExtensions::isVideoExtension(extension))
    {
        return m_videoDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }

    return Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "Unsupported media extension: " + extension));
}
