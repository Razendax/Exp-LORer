#include "BulkRenameUseCase.h"

#include <cstddef>
#include <string_view>
#include <utility>

#include "IFileSystemRepository.h"
#include "PathUtf8.h"
#include "UnicodeCaseFold.h"

namespace
{
    // Same forbidden-character set FileNameEditDelegate's forbiddenCharsPattern already blocks
    // client-side for single-item rename -- small enough to restate here rather than extract a
    // shared header, consistent with that regex already being file-local today.
    bool hasForbiddenCharacter(const std::string& nameUtf8)
    {
        constexpr std::string_view forbidden = "\\/:*?\"<>|";
        for (const char c : nameUtf8)
        {
            if (forbidden.find(c) != std::string_view::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::filesystem::path toPath(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }
}

BulkRenameUseCase::BulkRenameUseCase(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<BulkRenameUseCase::PreviewEntry>> BulkRenameUseCase::computePreview(
    const std::vector<FileNode>& selection, const BulkRenamePattern& pattern) const
{
    std::vector<PreviewEntry> entries;
    entries.reserve(selection.size());

    for (std::size_t i = 0; i < selection.size(); ++i)
    {
        const FileNode& file = selection[i];
        const std::string originalStemUtf8 = PathUtf8::toUtf8(file.path().stem());
        const std::string originalExtensionUtf8 = PathUtf8::toUtf8(file.path().extension());

        auto proposedName = pattern.apply(originalStemUtf8, originalExtensionUtf8, static_cast<int>(i));
        if (proposedName.hasError())
        {
            return Result<std::vector<PreviewEntry>>::failure(std::move(proposedName).error());
        }

        PreviewEntry entry;
        entry.source = file.path();
        entry.proposedPath = file.path().parent_path() / toPath(proposedName.value());
        entry.status = EntryStatus::Ok;
        entries.push_back(std::move(entry));
    }

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        PreviewEntry& entry = entries[i];

        if (hasForbiddenCharacter(PathUtf8::toUtf8(entry.proposedPath.filename())))
        {
            entry.status = EntryStatus::InvalidName;
            continue;
        }

        if (PathUtf8::toUtf8(entry.proposedPath.filename()) == PathUtf8::toUtf8(entry.source.filename()))
        {
            entry.status = EntryStatus::Unchanged;
            continue;
        }

        const std::string foldedProposed = UnicodeCaseFold::foldUtf8(PathUtf8::toUtf8(entry.proposedPath));
        bool collidesWithinBatch = false;
        for (std::size_t j = 0; j < entries.size() && !collidesWithinBatch; ++j)
        {
            if (j == i)
            {
                continue;
            }
            collidesWithinBatch = foldedProposed == UnicodeCaseFold::foldUtf8(PathUtf8::toUtf8(entries[j].proposedPath));
        }

        if (collidesWithinBatch)
        {
            entry.status = EntryStatus::CollisionWithinBatch;
            continue;
        }

        if (m_fileSystemRepository.stat(entry.proposedPath).hasValue())
        {
            entry.status = EntryStatus::CollisionWithExisting;
            continue;
        }

        entry.status = EntryStatus::Ok;
    }

    return Result<std::vector<PreviewEntry>>::success(std::move(entries));
}
