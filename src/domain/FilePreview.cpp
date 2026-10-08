#include "FilePreview.h"

FilePreview FilePreview::folder(std::vector<FileNode> entries)
{
    FilePreview preview(FilePreviewKind::Folder);
    preview.m_folderEntries = std::move(entries);
    return preview;
}

FilePreview FilePreview::text(std::string content, bool truncated, std::filesystem::path path)
{
    FilePreview preview(FilePreviewKind::Text);
    preview.m_text = std::move(content);
    preview.m_textTruncated = truncated;
    preview.m_path = std::move(path);
    return preview;
}

FilePreview FilePreview::image(std::vector<std::byte> encodedBytes, std::filesystem::path path, MediaMetadata metadata)
{
    FilePreview preview(FilePreviewKind::Image);
    preview.m_imageBytes = std::move(encodedBytes);
    preview.m_path = std::move(path);
    preview.m_metadata = std::move(metadata);
    return preview;
}

FilePreview FilePreview::video(std::vector<std::byte> encodedPosterBytes, std::filesystem::path path, MediaMetadata metadata)
{
    FilePreview preview(FilePreviewKind::Video);
    preview.m_imageBytes = std::move(encodedPosterBytes);
    preview.m_path = std::move(path);
    preview.m_metadata = std::move(metadata);
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
