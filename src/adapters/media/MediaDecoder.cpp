#include "MediaDecoder.h"

#include "MediaExtensions.h"

Result<MediaThumbnail> MediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                          int maxHeight, int previewIndex) const
{
    const std::string extension = MediaExtensions::lowercaseExtension(mediaFile);

    // Checked ahead of the generic isImageExtension branch below, since ".svg"/".gif" both also
    // satisfy that (Architecture.md §14.79). GIF is routed to QtGifImageDecoder rather than
    // VipsImageDecoder because this vcpkg build's libvips has no GIF loader enabled
    // (Architecture.md §14.81).
    if (MediaExtensions::isVectorImageExtension(extension))
    {
        return m_svgDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }
    if (MediaExtensions::isAnimatedImageExtension(extension))
    {
        return m_gifDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }
    if (MediaExtensions::isImageExtension(extension))
    {
        return m_imageDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }
    if (MediaExtensions::isVideoExtension(extension))
    {
        return m_videoDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }
    if (MediaExtensions::isPdfExtension(extension))
    {
        return m_pdfDecoder.generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex);
    }

    return Result<MediaThumbnail>::failure(Error(ErrorCode::InvalidArgument, "Unsupported media extension: " + extension));
}
