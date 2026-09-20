#include "FilePreview.h"

FilePreview FilePreview::folder(std::vector<FileNode> entries)
{
    FilePreview preview(FilePreviewKind::Folder);
    preview.m_folderEntries = std::move(entries);
    return preview;
}

FilePreview FilePreview::text(std::string content, bool truncated)
{
    FilePreview preview(FilePreviewKind::Text);
    preview.m_text = std::move(content);
    preview.m_textTruncated = truncated;
    return preview;
}

FilePreview FilePreview::image(std::vector<std::byte> encodedBytes)
{
    FilePreview preview(FilePreviewKind::Image);
    preview.m_imageBytes = std::move(encodedBytes);
    return preview;
}

FilePreview FilePreview::video(std::vector<std::byte> encodedPosterBytes)
{
    FilePreview preview(FilePreviewKind::Video);
    preview.m_imageBytes = std::move(encodedPosterBytes);
    return preview;
}

FilePreview FilePreview::unsupported()
{
    return FilePreview(FilePreviewKind::Unsupported);
}

FilePreview::FilePreview(FilePreviewKind kind)
    : m_kind(kind)
{
}
