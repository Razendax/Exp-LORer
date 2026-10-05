#include "WindowsFragmentationScanner.h"

#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#endif

#include "IFileSystemRepository.h"
#include "Logging.h"
#include "WindowsLongPath.h"

#include <chrono>

#ifdef _WIN32
namespace
{
    namespace fs = std::filesystem;

    // One DeviceIoControl(FSCTL_GET_RETRIEVAL_POINTERS) call's output buffer, sized for this many
    // extents -- large enough that most files resolve in a single call; a more-fragmented file just
    // loops (see countExtents's ERROR_MORE_DATA branch).
    constexpr std::size_t kExtentsPerCall = 512;
    constexpr std::size_t kExtentEntrySize = sizeof(LARGE_INTEGER) * 2; // one Extents[] entry: {NextVcn, Lcn}
    constexpr std::size_t kBufferSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (kExtentsPerCall - 1) * kExtentEntrySize;

    enum class ExtentQueryOutcome
    {
        Counted,
        Unsupported, // Non-NTFS volume, or any other unexpected DeviceIoControl failure -- caller skips this file.
    };

    struct ExtentQueryResult
    {
        ExtentQueryOutcome outcome;
        std::uint32_t fragmentCount = 0;
    };

    // Sums ExtentCount across as many FSCTL_GET_RETRIEVAL_POINTERS calls as needed (NTFS already
    // reports the minimal extent set, so this sum *is* the fragment count directly -- no
    // adjacency/merging logic needed).
    ExtentQueryResult countExtents(HANDLE handle)
    {
        std::vector<unsigned char> buffer(kBufferSize);
        auto* output = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(buffer.data());

        STARTING_VCN_INPUT_BUFFER input{};
        input.StartingVcn.QuadPart = 0;

        std::uint32_t fragmentCount = 0;

        for (;;)
        {
            DWORD bytesReturned = 0;
            const BOOL ok = DeviceIoControl(handle, FSCTL_GET_RETRIEVAL_POINTERS, &input, sizeof(input), output,
                                             static_cast<DWORD>(buffer.size()), &bytesReturned, nullptr);
            if (ok)
            {
                fragmentCount += output->ExtentCount;
                break;
            }

            const DWORD err = GetLastError();
            if (err == ERROR_MORE_DATA)
            {
                fragmentCount += output->ExtentCount;
                if (output->ExtentCount == 0)
                {
                    break; // Defensive: avoid an infinite loop if a volume ever reports this oddly.
                }
                input.StartingVcn = output->Extents[output->ExtentCount - 1].NextVcn;
                continue;
            }

            if (err == ERROR_HANDLE_EOF)
            {
                // Zero allocated extents (a file small enough to be resident entirely in its own
                // MFT record) -- finalized as fragmentCount 1 below, the most-contiguous case, not
                // an unknown one.
                break;
            }

            // ERROR_INVALID_FUNCTION/ERROR_NOT_SUPPORTED (non-NTFS volume: FAT32, exFAT) or any
            // other unexpected failure -- fragmentation unknown for this one file.
            return ExtentQueryResult{ ExtentQueryOutcome::Unsupported, 0 };
        }

        return ExtentQueryResult{ ExtentQueryOutcome::Counted, fragmentCount == 0 ? 1u : fragmentCount };
    }
}
#endif

WindowsFragmentationScanner::WindowsFragmentationScanner(IFileSystemRepository& fileSystemRepository)
    : m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<FileFragmentationInfo>> WindowsFragmentationScanner::scan(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
        onProgress,
    const std::atomic<bool>& cancelRequested) const
{
#ifdef _WIN32
    Logging::info("WindowsFragmentationScanner: scan started (minFileSizeBytes=" + std::to_string(minFileSizeBytes) + ")");
    const auto scanStart = std::chrono::steady_clock::now();

    // totalCandidates == 0 signals the listing phase to onProgress -- the eventual candidate
    // count isn't known until this enumeration finishes, so this is reported as indeterminate
    // progress rather than a fake total. listDirectoryRecursive already throttles this callback
    // to ~once/sec (Architecture.md §14.37).
    auto listingProgress = [&onProgress, &root](const std::vector<FileNode>& filesSoFar) {
        if (onProgress)
        {
            onProgress(filesSoFar.size(), 0, filesSoFar.empty() ? root : filesSoFar.back().path());
        }
    };

    auto listed = m_fileSystemRepository.listDirectoryRecursive(root, listingProgress, nullptr);
    if (listed.hasError())
    {
        Logging::warn("WindowsFragmentationScanner: scan failed: " + std::string(toLogString(listed.error().code)));
        return Result<std::vector<FileFragmentationInfo>>::failure(std::move(listed).error());
    }

    std::vector<FileNode> candidates;
    for (const FileNode& node : listed.value())
    {
        if (!node.isDirectory() && node.size() >= minFileSizeBytes)
        {
            candidates.push_back(node);
        }
    }

    std::vector<FileFragmentationInfo> results;
    results.reserve(candidates.size());

    std::size_t scanned = 0;
    std::size_t skippedCount = 0;
    for (const FileNode& candidate : candidates)
    {
        if (cancelRequested.load(std::memory_order_relaxed))
        {
            Logging::info("WindowsFragmentationScanner: scan cancelled");
            return Result<std::vector<FileFragmentationInfo>>::failure(Error(ErrorCode::Cancelled, "Fragmentation scan cancelled"));
        }

        // FILE_READ_ATTRIBUTES, not GENERIC_READ -- FSCTL_GET_RETRIEVAL_POINTERS only inspects NTFS
        // metadata, never the file's actual data, so no read-data access is needed. A lighter-weight
        // open: fewer access checks and less likely to conflict with a file another process holds
        // open for exclusive write.
        const fs::path nativePath = WindowsLongPath::withPrefix(candidate.path());
        HANDLE handle = CreateFileW(nativePath.wstring().c_str(), FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            const ExtentQueryResult extentResult = countExtents(handle);
            CloseHandle(handle);

            if (extentResult.outcome == ExtentQueryOutcome::Counted)
            {
                results.push_back(FileFragmentationInfo{ candidate.path(), candidate.size(), extentResult.fragmentCount });
            }
            else
            {
                // Unsupported (non-NTFS volume) -- skip this file from the report, not the whole scan.
                ++skippedCount;
            }
        }
        else
        {
            // Unopenable (e.g. permission denied) -- same per-file skip tolerance.
            ++skippedCount;
        }

        ++scanned;
        if (onProgress)
        {
            onProgress(scanned, candidates.size(), candidate.path());
        }
    }

    if (skippedCount > 0)
    {
        Logging::debug("WindowsFragmentationScanner: skipped " + std::to_string(skippedCount) + " file(s)");
    }

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - scanStart).count();
    Logging::info("WindowsFragmentationScanner: scan finished (" + std::to_string(candidates.size()) + " candidate(s), " +
                   std::to_string(results.size()) + " result(s) in " + std::to_string(elapsedMs) + "ms)");
    return Result<std::vector<FileFragmentationInfo>>::success(std::move(results));
#else
    (void)root;
    (void)minFileSizeBytes;
    (void)onProgress;
    (void)cancelRequested;
    return Result<std::vector<FileFragmentationInfo>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}
