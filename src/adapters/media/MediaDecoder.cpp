#include "MediaDecoder.h"

#include "MediaExtensions.h"

Result<MediaThumbnail> MediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                          int maxHeight) const
{
    const std::string extension = MediaExtensions::lowercaseExtension(mediaFile);

    if (MediaExtensions::isImageExtension(extension))
    {
        return m_imageDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight);
    }
    if (MediaExtensions::isVideoExtension(extension))
    {
        return m_videoDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight);
    }

    return Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "Unsupported media extension: " + extension));
}
