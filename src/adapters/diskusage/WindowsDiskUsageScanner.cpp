#include "WindowsDiskUsageScanner.h"

#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "Logging.h"
#include "PathUtf8.h"
#include "WindowsLongPath.h"

#ifdef _WIN32
namespace
{
    namespace fs = std::filesystem;

    using WindowsLongPath::withPrefix;

    std::uintmax_t sizeFromFindData(const WIN32_FIND_DATAW& data)
    {
        return (static_cast<std::uintmax_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }

    bool isReparsePoint(const WIN32_FIND_DATAW& data)
    {
        return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    bool isDirectoryEntry(const WIN32_FIND_DATAW& data)
    {
        return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    // Recursively walks `nativeDir` (already \\?\-prefixed as needed), pushing a DiskUsageNode
    // (outward path, never the \\?\-prefixed one) for every file and subdirectory under it into
    // `results`. A subdirectory's node is pushed *before* recursing into it, then its size is
    // patched in place once the recursive call returns (post-order aggregation) -- the index stays
    // valid across any reallocation `results` undergoes while recursing, unlike a pointer/reference
    // would. Returns the aggregate size of `nativeDir` itself. onProgress is throttled to roughly
    // once/second, mirroring StandardFileSystemRepository::listDirectoryRecursive. cancelRequested
    // is checked once per directory entry, at the top of this loop and at every recursion level, so
    // a cancellation unwinds the whole in-flight walk within one loop iteration per level (coarse,
    // not interruptible mid-syscall, same granularity WindowsFragmentationScanner checks at).
    std::uintmax_t walkDirectory(const fs::path& nativeDir, const fs::path& outwardDir, std::vector<DiskUsageNode>& results,
                                  std::size_t& scannedCount,
                                  const std::function<void(std::size_t, const fs::path&)>& onProgress,
                                  std::chrono::steady_clock::time_point& lastProgressTime, const std::atomic<bool>& cancelRequested,
                                  std::size_t& unreadableDirCount, std::size_t& reparsePointSkipCount)
    {
        std::uintmax_t total = 0;

        // FindFirstFileExW with FindExInfoBasic skips 8.3 short-name generation (not needed here)
        // and FIND_FIRST_EX_LARGE_FETCH requests a larger per-call result buffer from the kernel --
        // both cut syscall/CPU overhead noticeably over plain FindFirstFileW on directories with
        // many entries, which is the common case for a whole-drive scan.
        const std::wstring searchPattern = (nativeDir / L"*").wstring();
        WIN32_FIND_DATAW findData{};
        HANDLE handle = FindFirstFileExW(searchPattern.c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch,
                                          nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE)
        {
            ++unreadableDirCount;
            return 0;
        }

        do
        {
            if (cancelRequested.load(std::memory_order_relaxed))
            {
                break;
            }

            const std::wstring name = findData.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }

            const fs::path childNative = nativeDir / name;
            const fs::path childOutward = outwardDir / name;

            if (isDirectoryEntry(findData))
            {
                if (isReparsePoint(findData))
                {
                    // Junctions/symlinks are listed (so the user still sees them) but never
                    // recursed into -- avoids cycles and double-counted sizes -- and contribute 0
                    // bytes to the parent's aggregate, matching Windows Explorer's own treatment of
                    // a reparse point itself (see buildFileNode's identical rationale in
                    // StandardFileSystemRepository.cpp).
                    results.push_back(DiskUsageNode{ childOutward, PathUtf8::toUtf8(fs::path(name)), 0, true });
                    ++reparsePointSkipCount;
                    continue;
                }

                const std::size_t placeholderIndex = results.size();
                results.push_back(DiskUsageNode{ childOutward, PathUtf8::toUtf8(fs::path(name)), 0, true });

                const std::uintmax_t subtreeSize =
                    walkDirectory(childNative, childOutward, results, scannedCount, onProgress, lastProgressTime, cancelRequested,
                                  unreadableDirCount, reparsePointSkipCount);
                results[placeholderIndex].size = subtreeSize;
                total += subtreeSize;
            }
            else
            {
                const std::uintmax_t fileSize = sizeFromFindData(findData);
                results.push_back(DiskUsageNode{ childOutward, PathUtf8::toUtf8(fs::path(name)), fileSize, false });
                total += fileSize;
            }

            ++scannedCount;
            if (onProgress)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastProgressTime >= std::chrono::seconds(1))
                {
                    onProgress(scannedCount, childOutward);
                    lastProgressTime = now;
                }
            }
        } while (FindNextFileW(handle, &findData));

        FindClose(handle);
        return total;
    }
}
#endif

Result<std::vector<DiskUsageNode>> WindowsDiskUsageScanner::scan(
    const std::filesystem::path& root,
    const std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)>& onProgress,
    const std::atomic<bool>& cancelRequested) const
{
#ifdef _WIN32
    namespace fs = std::filesystem;
    const fs::path target = WindowsLongPath::withPrefix(root);

    std::error_code ec;
    if (!fs::is_directory(target, ec))
    {
        Logging::warn("WindowsDiskUsageScanner: scan failed: " + std::string(toLogString(ErrorCode::NotFound)));
        return Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(root) + " is not a directory"));
    }

    Logging::info("WindowsDiskUsageScanner: scan started");
    const auto scanStart = std::chrono::steady_clock::now();

    std::vector<DiskUsageNode> results;
    std::size_t scannedCount = 0;
    auto lastProgressTime = std::chrono::steady_clock::now();
    std::size_t unreadableDirCount = 0;
    std::size_t reparsePointSkipCount = 0;
    const std::uintmax_t totalSize = walkDirectory(target, root, results, scannedCount, onProgress, lastProgressTime,
                                                     cancelRequested, unreadableDirCount, reparsePointSkipCount);

    if (cancelRequested.load(std::memory_order_relaxed))
    {
        Logging::info("WindowsDiskUsageScanner: scan cancelled");
        return Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::Cancelled, "Disk usage scan cancelled"));
    }

    // Root itself, aggregate size. filename() is empty for a drive root ("C:\"), so fall back to
    // the full root text in that case (same quirk StandardFileSystemRepository::buildDriveNode
    // already works around via withDisplayName).
    std::string rootName = PathUtf8::toUtf8(root.filename());
    if (rootName.empty())
    {
        rootName = PathUtf8::toUtf8(root);
    }
    results.insert(results.begin(), DiskUsageNode{ root, rootName, totalSize, true });

    if (unreadableDirCount > 0)
    {
        Logging::debug("WindowsDiskUsageScanner: " + std::to_string(unreadableDirCount) + " unreadable director(y/ies) tolerated");
    }
    if (reparsePointSkipCount > 0)
    {
        Logging::debug("WindowsDiskUsageScanner: " + std::to_string(reparsePointSkipCount) + " reparse point(s) skipped");
    }

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - scanStart).count();
    Logging::info("WindowsDiskUsageScanner: scan finished (" + std::to_string(results.size()) + " node(s), " +
                   std::to_string(totalSize) + " byte(s) in " + std::to_string(elapsedMs) + "ms)");
    return Result<std::vector<DiskUsageNode>>::success(std::move(results));
#else
    (void)root;
    (void)onProgress;
    (void)cancelRequested;
    return Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}
