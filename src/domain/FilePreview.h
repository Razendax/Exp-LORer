#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "FileNode.h"

enum class FilePreviewKind
{
    Folder,
    Text,
    Image,
    Video,
    Unsupported,
};

// Entity representing the right panel's Preview-tab content for one resolved target
// (Architecture.md §14.25). Plain entity, no framework deps, same posture as MediaMetadata.
class FilePreview
{
public:
    static FilePreview folder(std::vector<FileNode> entries);
    static FilePreview text(std::string content, bool truncated);
    static FilePreview image(std::vector<std::byte> encodedBytes);
    static FilePreview video(std::vector<std::byte> encodedPosterBytes);
    static FilePreview unsupported();

    FilePreviewKind kind() const noexcept { return m_kind; }

    // Folder only; empty for every other kind.
    const std::vector<FileNode>& folderEntries() const noexcept { return m_folderEntries; }

    // Text only; empty for every other kind.
    const std::string& text() const noexcept { return m_text; }

    // Text only; false for every other kind.
    bool textTruncated() const noexcept { return m_textTruncated; }

    // Image or Video only (the encoded thumbnail/poster-frame bytes); empty for every other kind.
    const std::vector<std::byte>& imageBytes() const noexcept { return m_imageBytes; }

private:
    explicit FilePreview(FilePreviewKind kind);

    FilePreviewKind m_kind;
    std::vector<FileNode> m_folderEntries;
    std::string m_text;
    bool m_textTruncated = false;
    std::vector<std::byte> m_imageBytes;
};
