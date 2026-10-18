#include "TextFileEditUseCase.h"

#include <cstring>

#include "ArchiveExtensions.h"
#include "IFileSystemRepository.h"

TextFileEditUseCase::TextFileEditUseCase(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::string> TextFileEditUseCase::readFullText(const std::filesystem::path& path) const
{
    // path may name an entry *inside* an archive (Architecture.md §14.28) rather than a real
    // on-disk file -- same materialize-before-reading precedent FilePreviewUseCase::
    // generatePreview follows before readFilePrefix.
    std::filesystem::path readablePath = path;
    if (ArchiveExtensions::archiveAncestorInPath(path))
    {
        auto materialized = m_fileSystemRepository.materializeForReading(path);
        if (!materialized)
        {
            return Result<std::string>::failure(materialized.error());
        }
        readablePath = std::move(materialized).value();
    }

    auto bytes = m_fileSystemRepository.readFile(readablePath);
    if (!bytes)
    {
        return Result<std::string>::failure(bytes.error());
    }

    const std::vector<std::byte>& content = bytes.value();
    return Result<std::string>::success(std::string(reinterpret_cast<const char*>(content.data()), content.size()));
}

Result<void> TextFileEditUseCase::writeText(const std::filesystem::path& path, const std::string& content) const
{
    std::vector<std::byte> bytes(content.size());
    if (!content.empty())
    {
        std::memcpy(bytes.data(), content.data(), content.size());
    }

    return m_fileSystemRepository.writeFile(path, bytes);
}
