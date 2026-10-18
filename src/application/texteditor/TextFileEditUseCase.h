#pragma once

#include <filesystem>
#include <string>

#include "Result.h"

class IFileSystemRepository;

// Backs the Preview panel's text editor Edit/Save actions (Architecture.md §14.98) -- a thin,
// stateless use case over IFileSystemRepository, mirroring FilePreviewUseCase's own "depend on
// Ports only" shape (§11).
class TextFileEditUseCase
{
public:
    explicit TextFileEditUseCase(IFileSystemRepository& fileSystemRepository);

    // Resolves path to a real, readable file (materializing out of an archive if needed, same
    // precedent FilePreviewUseCase::generatePreview follows before readFilePrefix) and reads its
    // full contents as UTF-8 text.
    Result<std::string> readFullText(const std::filesystem::path& path) const;

    // Writes content to path directly -- never materialized, since a save must target the real
    // file, not a temp-extracted archive copy.
    Result<void> writeText(const std::filesystem::path& path, const std::string& content) const;

private:
    IFileSystemRepository& m_fileSystemRepository;
};
