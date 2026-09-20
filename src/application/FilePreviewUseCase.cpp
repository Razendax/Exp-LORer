#include "FilePreviewUseCase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "IFileSystemRepository.h"
#include "IMediaDecoder.h"
#include "MediaExtensions.h"
#include "PathUtf8.h"

namespace
{
    namespace fs = std::filesystem;

    bool isTextExtension(const std::string& extension)
    {
        static const std::array<std::string, 26> kTextExtensions{
            ".txt", ".html", ".htm", ".xml", ".json", ".md",  ".ini", ".log", ".yaml", ".yml", ".csv",  ".cs",  ".cpp",
            ".h",   ".hpp",  ".c",   ".py",  ".js",   ".ts",  ".css", ".sql", ".sh",   ".bat", ".ps1",  ".xaml", ".toml",
        };
        return std::find(kTextExtensions.begin(), kTextExtensions.end(), extension) != kTextExtensions.end();
    }

    bool caseInsensitiveLess(const FileNode& lhs, const FileNode& rhs)
    {
        const std::string lhsName = PathUtf8::toUtf8(lhs.name());
        const std::string rhsName = PathUtf8::toUtf8(rhs.name());
        return std::lexicographical_compare(lhsName.begin(), lhsName.end(), rhsName.begin(), rhsName.end(),
                                             [](unsigned char a, unsigned char b) { return std::tolower(a) < std::tolower(b); });
    }
}

FilePreviewUseCase::FilePreviewUseCase(IFileSystemRepository& fileSystemRepository, IMediaDecoder& mediaDecoder)
    : m_fileSystemRepository(fileSystemRepository)
    , m_mediaDecoder(mediaDecoder)
{
}

Result<FilePreview> FilePreviewUseCase::generatePreview(const FileNode& target, int maxImageWidth, int maxImageHeight,
                                                          std::size_t maxTextBytes) const
{
    if (target.isDirectory())
    {
        auto listing = m_fileSystemRepository.listDirectory(target.path());
        if (!listing)
        {
            return Result<FilePreview>::failure(listing.error());
        }

        std::vector<FileNode> entries = std::move(listing).value();
        std::sort(entries.begin(), entries.end(), [](const FileNode& lhs, const FileNode& rhs) {
            if (lhs.isDirectory() != rhs.isDirectory())
            {
                return lhs.isDirectory();
            }
            return caseInsensitiveLess(lhs, rhs);
        });

        return Result<FilePreview>::success(FilePreview::folder(std::move(entries)));
    }

    const std::string extension = MediaExtensions::lowercaseExtension(target.path());

    if (MediaExtensions::isImageExtension(extension))
    {
        auto bytes = m_mediaDecoder.generateThumbnail(target.path(), maxImageWidth, maxImageHeight);
        if (!bytes)
        {
            return Result<FilePreview>::failure(bytes.error());
        }
        return Result<FilePreview>::success(FilePreview::image(std::move(bytes).value()));
    }

    if (MediaExtensions::isVideoExtension(extension))
    {
        auto bytes = m_mediaDecoder.generateThumbnail(target.path(), maxImageWidth, maxImageHeight);
        if (!bytes)
        {
            return Result<FilePreview>::failure(bytes.error());
        }
        return Result<FilePreview>::success(FilePreview::video(std::move(bytes).value()));
    }

    if (isTextExtension(extension))
    {
        auto prefix = m_fileSystemRepository.readFilePrefix(target.path(), maxTextBytes);
        if (!prefix)
        {
            return Result<FilePreview>::failure(prefix.error());
        }

        const std::vector<std::byte>& bytes = prefix.value();
        const bool containsNul = std::any_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{ 0 }; });
        if (containsNul)
        {
            return Result<FilePreview>::success(FilePreview::unsupported());
        }

        std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        // bytes.size() alone can't distinguish "file is exactly maxTextBytes long" from "file is
        // longer and got capped" -- both read exactly maxTextBytes. target.size() (from the prior
        // stat) gives the true file size to compare against.
        const bool truncated = target.size() > maxTextBytes;
        return Result<FilePreview>::success(FilePreview::text(std::move(content), truncated));
    }

    return Result<FilePreview>::success(FilePreview::unsupported());
}
