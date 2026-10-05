#include "StandardDuplicateFileScanner.h"

#include <array>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>

#include "IFileSystemRepository.h"
#include "StandardFileHasher.h"
#include "WindowsLongPath.h"

namespace
{
    // Buffered byte-for-byte comparison, early-exiting on the first difference (or the first I/O
    // failure, surfaced as an error rather than a silent "not equal"). The caller treats a failure
    // the same as "not equal" -- conservative, since this feeds a delete action.
    Result<bool> filesByteIdentical(const std::filesystem::path& first, const std::filesystem::path& second)
    {
        const std::filesystem::path nativeFirst = WindowsLongPath::withPrefix(first);
        const std::filesystem::path nativeSecond = WindowsLongPath::withPrefix(second);

        std::ifstream streamFirst(nativeFirst, std::ios::binary);
        std::ifstream streamSecond(nativeSecond, std::ios::binary);
        if (!streamFirst || !streamSecond)
        {
            return Result<bool>::failure(Error(ErrorCode::IoError, "Failed to open file for byte comparison"));
        }

        constexpr std::size_t chunkSize = 256 * 1024;
        std::array<char, chunkSize> bufferFirst{};
        std::array<char, chunkSize> bufferSecond{};

        while (streamFirst && streamSecond)
        {
            streamFirst.read(bufferFirst.data(), static_cast<std::streamsize>(chunkSize));
            streamSecond.read(bufferSecond.data(), static_cast<std::streamsize>(chunkSize));

            const auto readFirst = static_cast<std::size_t>(streamFirst.gcount());
            const auto readSecond = static_cast<std::size_t>(streamSecond.gcount());

            if (readFirst != readSecond)
            {
                return Result<bool>::success(false);
            }
            if (readFirst == 0)
            {
                break;
            }
            if (std::memcmp(bufferFirst.data(), bufferSecond.data(), readFirst) != 0)
            {
                return Result<bool>::success(false);
            }
        }

        return Result<bool>::success(true);
    }
}

StandardDuplicateFileScanner::StandardDuplicateFileScanner(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<DuplicateFileGroup>> StandardDuplicateFileScanner::scan(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
    const std::atomic<bool>& cancelRequested) const
{
    auto listed = m_fileSystemRepository.listDirectoryRecursive(root, nullptr, nullptr);
    if (listed.hasError())
    {
        return Result<std::vector<DuplicateFileGroup>>::failure(std::move(listed).error());
    }

    // Stage 1: bucket by exact size. A unique size can never have a duplicate, so buckets with
    // fewer than 2 entries are dropped immediately -- no hashing wasted on them.
    std::unordered_map<std::uintmax_t, std::vector<std::filesystem::path>> bySize;
    for (const auto& node : listed.value())
    {
        if (node.isDirectory() || node.size() < minFileSizeBytes)
        {
            continue;
        }
        bySize[node.size()].push_back(node.path());
    }

    std::size_t totalCandidates = 0;
    for (const auto& [size, paths] : bySize)
    {
        if (paths.size() >= 2)
        {
            totalCandidates += paths.size();
        }
    }

    std::vector<DuplicateFileGroup> groups;
    std::size_t hashedSoFar = 0;

    for (auto& [size, paths] : bySize)
    {
        if (paths.size() < 2)
        {
            continue;
        }

        // Stage 2: sub-bucket same-size files by full-content hash.
        std::unordered_map<std::uint64_t, std::vector<std::filesystem::path>> byHash;
        for (const auto& path : paths)
        {
            if (cancelRequested.load(std::memory_order_relaxed))
            {
                return Result<std::vector<DuplicateFileGroup>>::failure(Error(ErrorCode::Cancelled, "Duplicate scan cancelled"));
            }

            auto hashed = StandardFileHasher::hashFileContent(path);
            ++hashedSoFar;
            if (onProgress)
            {
                onProgress(hashedSoFar, totalCandidates);
            }

            if (hashed.hasError())
            {
                // Unreadable file: skip it from duplicate consideration rather than failing the
                // whole scan, same tolerance posture as listDirectoryRecursive's own per-entry
                // skip-unreadable-subtrees behavior.
                continue;
            }

            byHash[hashed.value()].push_back(path);
        }

        // Stage 3: byte-for-byte verify every same-(size,hash) bucket before confirming it as a
        // duplicate group. Candidates are compared against each confirmed group's first file; a
        // file that disagrees splits off into its own separate confirmed group instead of being
        // silently merged or dropped.
        for (auto& [hash, hashPaths] : byHash)
        {
            if (hashPaths.size() < 2)
            {
                continue;
            }

            std::vector<std::vector<std::filesystem::path>> confirmedGroups;
            for (const auto& candidate : hashPaths)
            {
                bool placed = false;
                for (auto& confirmed : confirmedGroups)
                {
                    auto areEqual = filesByteIdentical(confirmed.front(), candidate);
                    if (areEqual.hasValue() && areEqual.value())
                    {
                        confirmed.push_back(candidate);
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                {
                    confirmedGroups.push_back({ candidate });
                }
            }

            for (auto& confirmed : confirmedGroups)
            {
                if (confirmed.size() < 2)
                {
                    continue;
                }
                groups.push_back(DuplicateFileGroup{ hash, size, std::move(confirmed) });
            }
        }
    }

    return Result<std::vector<DuplicateFileGroup>>::success(std::move(groups));
}
