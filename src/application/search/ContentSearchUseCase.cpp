#include "ContentSearchUseCase.h"

#include "ArchiveExtensions.h"
#include "BinaryContentDetector.h"
#include "FileNavigationUseCase.h"
#include "IFileSystemRepository.h"
#include "PathUtf8.h"
#include "RegexMatch.h"
#include "SubstringMatch.h"
#include "WholeWordMatch.h"

namespace
{
    std::vector<std::string> splitLines(const std::vector<std::byte>& bytes)
    {
        const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        std::vector<std::string> lines;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t newlinePos = content.find('\n', start);
            const std::size_t end = newlinePos == std::string::npos ? content.size() : newlinePos;

            std::string line = content.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(std::move(line));

            if (newlinePos == std::string::npos)
            {
                break;
            }
            start = newlinePos + 1;
        }

        return lines;
    }
}

ContentSearchUseCase::ContentSearchUseCase(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository)
    : m_fileNavigationUseCase(fileNavigationUseCase)
    , m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<RawFileLines>> ContentSearchUseCase::collectSearchableContent(
    const std::filesystem::path& root, const std::function<bool(const std::filesystem::path&)>& shouldPrune, bool showHidden,
    const std::string& includePattern, const TextMatchOptions& includeOptions, const std::string& excludePattern,
    const TextMatchOptions& excludeOptions, const std::function<void(const std::vector<FileNode>&)>& onProgress) const
{
    auto listed = m_fileNavigationUseCase.listDirectoryRecursive(root, onProgress, shouldPrune);
    if (!listed)
    {
        return Result<std::vector<RawFileLines>>::failure(listed.error());
    }

    std::vector<RawFileLines> result;
    for (const FileNode& node : listed.value())
    {
        if (node.isDirectory())
        {
            continue;
        }
        if (!showHidden && node.isHidden())
        {
            continue;
        }
        if (ArchiveExtensions::isArchiveExtension(node.path()))
        {
            continue;
        }

        const std::string relative = PathUtf8::toUtf8(std::filesystem::relative(node.path(), root));
        if (!includePattern.empty() && !textMatches(relative, includePattern, includeOptions))
        {
            continue;
        }
        if (!excludePattern.empty() && textMatches(relative, excludePattern, excludeOptions))
        {
            continue;
        }

        auto prefix = m_fileSystemRepository.readFilePrefix(node.path(), kMaxContentSearchBytes);
        if (!prefix)
        {
            // A single-file read/stat failure (e.g. deleted mid-scan) is dropped rather than
            // failing the whole scan -- same tolerate-and-continue posture the recursive walk
            // itself already has for unreadable subtrees.
            continue;
        }
        if (BinaryContentDetector::containsNulByte(prefix.value()))
        {
            continue;
        }

        result.push_back(RawFileLines{ node, splitLines(prefix.value()) });
    }

    return Result<std::vector<RawFileLines>>::success(std::move(result));
}

std::vector<FileContentMatch> ContentSearchUseCase::filterLines(const std::vector<RawFileLines>& rawFiles,
                                                                   const std::string& queryUtf8,
                                                                   const TextMatchOptions& queryOptions)
{
    std::vector<FileContentMatch> result;
    if (queryUtf8.empty())
    {
        return result;
    }

    for (const RawFileLines& file : rawFiles)
    {
        std::vector<LineMatch> matchedLines;
        for (std::size_t i = 0; i < file.lines.size(); ++i)
        {
            if (textMatches(file.lines[i], queryUtf8, queryOptions))
            {
                matchedLines.push_back(LineMatch{ static_cast<int>(i + 1), file.lines[i] });
            }
        }

        if (!matchedLines.empty())
        {
            result.push_back(FileContentMatch{ file.node, std::move(matchedLines) });
        }
    }

    return result;
}

bool ContentSearchUseCase::textMatches(const std::string& candidateUtf8, const std::string& patternUtf8,
                                         const TextMatchOptions& options)
{
    switch (options.mode)
    {
        case TextMatchMode::WholeWord:
            return WholeWordMatch::matches(patternUtf8, candidateUtf8, options.caseSensitive);

        case TextMatchMode::Regex:
        {
            const Result<bool> matched = RegexMatch::matches(patternUtf8, candidateUtf8, options.caseSensitive);
            return matched.hasValue() && matched.value();
        }

        case TextMatchMode::Contains:
        default:
            return SubstringMatch::matches(patternUtf8, candidateUtf8, options.caseSensitive);
    }
}
