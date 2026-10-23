#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ContentSearchMatch.h"
#include "ContentSearchOptions.h"
#include "FileNode.h"
#include "Result.h"

class FileNavigationUseCase;
class IFileSystemRepository;

// Scans a directory tree and extracts searchable text content for the "Find in Files" left-panel
// tab (Architecture.md §14.104). Stateless, depends directly on Ports/FileNavigationUseCase (never
// owns a GitStatusUseCase, which is thread-affine) -- same posture as FilePreviewUseCase. A single
// shared instance may serve concurrent callers.
class ContentSearchUseCase
{
public:
    // Fixed prefix read per file -- bounds *search* completeness, not display cost, so it's
    // independent of and larger than FilePreviewViewModel's own 64 KiB preview cap. A match beyond
    // this prefix is not found; accepted per spec.
    static constexpr std::size_t kMaxContentSearchBytes = 1 * 1024 * 1024;

    ContentSearchUseCase(FileNavigationUseCase& fileNavigationUseCase, IFileSystemRepository& fileSystemRepository);

    // The I/O-bound half: walks root (pruned by shouldPrune, built by the caller -- see
    // ContentSearchWorker, which owns the thread-affine GitStatusUseCase this needs for
    // skip-git-ignored pruning), skips hidden entries when !showHidden, skips archives, applies
    // Include/Exclude against each candidate's path relative to root, reads up to
    // kMaxContentSearchBytes of each surviving file, skips binary files, and splits the rest into
    // lines. The query itself is not applied here at all -- see filterLines below.
    Result<std::vector<RawFileLines>> collectSearchableContent(
        const std::filesystem::path& root, const std::function<bool(const std::filesystem::path&)>& shouldPrune,
        bool showHidden, const std::string& includePattern, const TextMatchOptions& includeOptions,
        const std::string& excludePattern, const TextMatchOptions& excludeOptions,
        const std::function<void(const std::vector<FileNode>&)>& onProgress = nullptr) const;

    // The pure, cheap, no-I/O half: applies queryUtf8 (empty returns {}, per spec's "an empty
    // search bar performs no search") against every line of every already-scanned file. Used both
    // for a fresh scan's first display and for re-filtering in memory when only the main query's
    // Match Case/Whole Word/Regex toggle changes (no text change, no rescan).
    static std::vector<FileContentMatch> filterLines(const std::vector<RawFileLines>& rawFiles,
                                                       const std::string& queryUtf8, const TextMatchOptions& queryOptions);

    // Mode-dispatch function for matching a line of file *content* against the main query. Contains
    // mode is a literal substring match -- '*'/'?' have no special meaning in file content. An
    // invalid Regex pattern is treated as "no match" rather than propagated -- same accepted posture
    // NameSearchOptions's own Regex mode already documents. Declared here (not file-local) so it can
    // be unit-tested directly.
    static bool textMatches(const std::string& candidateUtf8, const std::string& patternUtf8, const TextMatchOptions& options);

    // Mode-dispatch function for matching a relative *path* against an Include/Exclude pattern.
    // Same WholeWord/Regex handling as textMatches, but Contains mode treats '*'/'?' as glob
    // wildcards (expanding a wildcard-free pattern to "*pattern*") -- the same convention
    // FileNavigationUseCase::filterByName's own Contains mode already uses for filenames, since
    // Include/Exclude patterns name files too (the UI's "e.g. *.cpp" hint only makes sense with this
    // behavior). Declared here (not file-local) so it can be unit-tested directly.
    static bool pathMatches(const std::string& candidateUtf8, const std::string& patternUtf8, const TextMatchOptions& options);

private:
    FileNavigationUseCase& m_fileNavigationUseCase;
    IFileSystemRepository& m_fileSystemRepository;
};
