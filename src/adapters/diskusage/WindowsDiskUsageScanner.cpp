#include "WindowsDiskUsageScanner.h"

#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

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
    // once/second, mirroring StandardFileSystemRepository::listDirectoryRecursive.
    std::uintmax_t walkDirectory(const fs::path& nativeDir, const fs::path& outwardDir, std::vector<DiskUsageNode>& results,
                                  const std::function<void()>& onProgress, std::chrono::steady_clock::time_point& lastProgressTime)
    {
        std::uintmax_t total = 0;

        const std::wstring searchPattern = (nativeDir / L"*").wstring();
        WIN32_FIND_DATAW findData{};
        HANDLE handle = FindFirstFileW(searchPattern.c_str(), &findData);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        do
        {
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
                    continue;
                }

                const std::size_t placeholderIndex = results.size();
                results.push_back(DiskUsageNode{ childOutward, PathUtf8::toUtf8(fs::path(name)), 0, true });

                const std::uintmax_t subtreeSize = walkDirectory(childNative, childOutward, results, onProgress, lastProgressTime);
                results[placeholderIndex].size = subtreeSize;
                total += subtreeSize;
            }
            else
            {
                const std::uintmax_t fileSize = sizeFromFindData(findData);
                results.push_back(DiskUsageNode{ childOutward, PathUtf8::toUtf8(fs::path(name)), fileSize, false });
                total += fileSize;
            }

            if (onProgress)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastProgressTime >= std::chrono::seconds(1))
                {
                    onProgress();
                    lastProgressTime = now;
                }
            }
        } while (FindNextFileW(handle, &findData));

        FindClose(handle);
        return total;
    }
}
#endif

Result<std::vector<DiskUsageNode>> WindowsDiskUsageScanner::scan(const std::filesystem::path& root,
                                                                   const std::function<void()>& onProgress) const
{
#ifdef _WIN32
    namespace fs = std::filesystem;
    const fs::path target = WindowsLongPath::withPrefix(root);

    std::error_code ec;
    if (!fs::is_directory(target, ec))
    {
        return Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(root) + " is not a directory"));
    }

    std::vector<DiskUsageNode> results;
    auto lastProgressTime = std::chrono::steady_clock::now();
    const std::uintmax_t totalSize = walkDirectory(target, root, results, onProgress, lastProgressTime);

    // Root itself, aggregate size. filename() is empty for a drive root ("C:\"), so fall back to
    // the full root text in that case (same quirk StandardFileSystemRepository::buildDriveNode
    // already works around via withDisplayName).
    std::string rootName = PathUtf8::toUtf8(root.filename());
    if (rootName.empty())
    {
        rootName = PathUtf8::toUtf8(root);
    }
    results.insert(results.begin(), DiskUsageNode{ root, rootName, totalSize, true });

    return Result<std::vector<DiskUsageNode>>::success(std::move(results));
#else
    (void)root;
    (void)onProgress;
    return Result<std::vector<DiskUsageNode>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}
