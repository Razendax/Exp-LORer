#include "MediaDecoder.h"

#include "MediaExtensions.h"

Result<MediaMetadata> MediaDecoder::extractMetadata(const std::filesystem::path& mediaFile) const
{
    const std::string extension = MediaExtensions::lowercaseExtension(mediaFile);

    if (MediaExtensions::isImageExtension(extension))
    {
        return m_imageDecoder.extractMetadata(mediaFile);
    }
    if (MediaExtensions::isVideoExtension(extension))
    {
        return m_videoDecoder.extractMetadata(mediaFile);
    }

    return Result<MediaMetadata>::failure(Error(ErrorCode::InvalidArgument, "Unsupported media extension: " + extension));
}

Result<std::vector<std::byte>> MediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
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

    return Result<std::vector<std::byte>>::failure(
        Error(ErrorCode::InvalidArgument, "Unsupported media extension: " + extension));
}
