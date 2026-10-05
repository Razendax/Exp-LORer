#include "StandardFileSystemRepository.h"

#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Windows.h>

#include <Shellapi.h>
#include <shlobj.h>
#endif

#include <xxhash.h>

#include "ArchivePathResolver.h"
#include "DriveLabel.h"
#include "LibArchiveReader.h"
#include "Logging.h"
#include "PathUtf8.h"
#include "VirtualPaths.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    using WindowsLongPath::withPrefix;

#ifdef _WIN32
    std::string utf8FromWide(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                              nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr,
                             nullptr);
        return result;
    }
#endif

    // fs::directory_entry(path) is a throwing constructor -- it refresh()es the entry immediately,
    // which throws filesystem_error ("directory_entry::refresh: Access is denied") for paths whose
    // attributes can't be queried (e.g. certain restricted system folders/junctions), even though
    // the path itself exists. The error_code overload never throws; buildFileNode() already
    // tolerates an entry whose cached status is empty since all of its own entry queries go through
    // error_code overloads too.
    fs::directory_entry makeDirectoryEntry(const fs::path& target)
    {
        std::error_code ec;
        return fs::directory_entry(target, ec);
    }

    FileType toFileType(const fs::directory_entry& entry)
    {
        std::error_code ec;
        if (entry.is_symlink(ec))
        {
            return FileType::Symlink;
        }
        ec.clear();
        if (entry.is_directory(ec))
        {
            return FileType::Directory;
        }
        ec.clear();
        if (entry.is_regular_file(ec))
        {
            return FileType::Regular;
        }
        return FileType::Other;
    }

    // Windows: the actual FILE_ATTRIBUTE_HIDDEN bit. Other platforms (future Linux backend):
    // the conventional leading-dot filename fallback.
    bool detectHidden(const fs::directory_entry& entry)
    {
#ifdef _WIN32
        const DWORD attrs = GetFileAttributesW(entry.path().c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
        const std::string filename = entry.path().filename().string();
        return !filename.empty() && filename.front() == '.';
#endif
    }

    // std::filesystem exposes only last-write time portably; creation time is approximated with
    // it since std::filesystem has no portable creation-time accessor.
    Result<FileNode> buildFileNode(const fs::path& outwardPath, const fs::directory_entry& entry)
    {
        std::error_code ec;
        const bool isSymlink = entry.is_symlink(ec);
        ec.clear();

        // is_regular_file()/file_size() follow the link to the target, which would report the
        // target's size for a symlink; report 0 instead so the link itself (not what it points
        // to) is what's described, matching Windows Explorer's treatment of reparse points.
        const auto size = (!isSymlink && entry.is_regular_file(ec)) ? entry.file_size(ec) : 0;
        ec.clear();

        const auto lastWrite = entry.last_write_time(ec);
        if (ec && !isSymlink)
        {
            return Result<FileNode>::failure(Error(ErrorCode::IoError, ec.message()));
        }

        // A symlink whose target is missing or inaccessible cannot have its target stat'd
        // (last_write_time follows the link), but it must still appear in listings rather than
        // silently vanishing — fall back to the epoch instead of failing the whole entry.
        const auto modificationTime = ec ? std::chrono::system_clock::time_point{}
                                          : std::chrono::clock_cast<std::chrono::system_clock>(lastWrite);
        return FileNode::create(outwardPath, size, modificationTime, modificationTime, toFileType(entry),
                                 detectHidden(entry));
    }

#ifdef _WIN32
    // Resolves one "This PC" quick-access folder via SHGetKnownFolderPath. No displayName
    // override — the real folder name ("Downloads", "Documents", ...) is already correct.
    Result<FileNode> buildKnownFolderNode(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &rawPath);
        if (FAILED(hr) || rawPath == nullptr)
        {
            return Result<FileNode>::failure(Error(ErrorCode::NotFound, "Known folder is not available"));
        }

        const fs::path folderPath(rawPath);
        CoTaskMemFree(rawPath);

        return buildFileNode(folderPath, makeDirectoryEntry(folderPath));
    }

    // Shared by buildDriveNode/driveSpaceInfo: resolves one drive letter's display label via
    // GetDriveTypeW + GetVolumeInformationW + DriveLabel::driveDisplayLabel ("Local Disk (C:)"-
    // style text). Returns nullopt if the letter has no present drive (DRIVE_NO_ROOT_DIR/
    // DRIVE_UNKNOWN). GetVolumeInformationW failing (e.g. an empty optical drive) is tolerated and
    // falls back to a type-based label rather than failing outright.
    std::optional<std::wstring> resolveDriveLabel(wchar_t letter)
    {
        const std::wstring rootPath = std::wstring(1, letter) + L":\\";
        const UINT driveType = GetDriveTypeW(rootPath.c_str());
        if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
        {
            return std::nullopt;
        }

        wchar_t volumeName[MAX_PATH + 1] = {};
        const BOOL gotVolumeInfo = GetVolumeInformationW(rootPath.c_str(), volumeName,
                                                           static_cast<DWORD>(sizeof(volumeName) / sizeof(wchar_t)),
                                                           nullptr, nullptr, nullptr, nullptr, 0);
        const std::wstring volumeLabel = gotVolumeInfo ? std::wstring(volumeName) : std::wstring();
        return DriveLabel::driveDisplayLabel(letter, driveType, volumeLabel);
    }

    // Resolves one drive letter to a synthetic FileNode, or nullopt if the letter has no present
    // drive (see resolveDriveLabel()).
    std::optional<FileNode> buildDriveNode(wchar_t letter)
    {
        auto label = resolveDriveLabel(letter);
        if (!label)
        {
            return std::nullopt;
        }

        const std::wstring rootPath = std::wstring(1, letter) + L":\\";
        auto node = FileNode::create(fs::path(rootPath), 0, std::chrono::system_clock::now(),
                                      std::chrono::system_clock::now(), FileType::Directory);
        if (!node)
        {
            return std::nullopt;
        }

        return node.value().withDisplayName(utf8FromWide(*label));
    }
#endif

    Error toError(const fs::filesystem_error& e)
    {
        const auto code = e.code();
        ErrorCode errorCode = ErrorCode::IoError;
        if (code == std::errc::no_such_file_or_directory)
        {
            errorCode = ErrorCode::NotFound;
        }
        else if (code == std::errc::file_exists)
        {
            errorCode = ErrorCode::AlreadyExists;
        }
        return Error(errorCode, e.what());
    }

#ifdef _WIN32
    Error win32CopyError(DWORD lastError)
    {
        if (lastError == ERROR_REQUEST_ABORTED)
        {
            return Error(ErrorCode::Cancelled, "Copy cancelled");
        }
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
        {
            return Error(ErrorCode::NotFound, "Source file not found");
        }
        if (lastError == ERROR_FILE_EXISTS || lastError == ERROR_ALREADY_EXISTS)
        {
            return Error(ErrorCode::AlreadyExists, "Destination already exists");
        }
        return Error(ErrorCode::IoError, "Failed to copy file (Win32 error " + std::to_string(lastError) + ")");
    }

    // LPPROGRESS_ROUTINE trampoline: lpData is a pointer to the caller's onChunk callback, passed
    // straight through by copyFileWithProgress below. Returning PROGRESS_CANCEL aborts the copy --
    // CopyFileExW itself then deletes the partial destination file (documented Windows behavior),
    // so no manual cleanup is needed here or in the caller.
    DWORD CALLBACK copyProgressTrampoline(LARGE_INTEGER /*totalFileSize*/, LARGE_INTEGER totalBytesTransferred,
                                           LARGE_INTEGER /*streamSize*/, LARGE_INTEGER /*streamBytesTransferred*/,
                                           DWORD /*streamNumber*/, DWORD /*callbackReason*/, HANDLE /*hSourceFile*/,
                                           HANDLE /*hDestinationFile*/, LPVOID lpData)
    {
        const auto* onChunk = static_cast<const std::function<bool(std::uint64_t)>*>(lpData);
        const bool keepGoing = (*onChunk)(static_cast<std::uint64_t>(totalBytesTransferred.QuadPart));
        return keepGoing ? PROGRESS_CONTINUE : PROGRESS_CANCEL;
    }
#endif
}

Result<std::vector<FileNode>> StandardFileSystemRepository::listDirectory(const std::filesystem::path& directory) const
{
    if (directory == VirtualPaths::ThisPC)
    {
        return listThisPc();
    }

    if (auto resolution = ArchivePathResolver::resolve(directory))
    {
        return listArchiveDirectory(directory, *resolution);
    }

    const fs::path target = withPrefix(directory);

    std::error_code ec;
    if (!fs::is_directory(target, ec))
    {
        Logging::warn("listDirectory failed: " + std::string(toLogString(ErrorCode::NotFound)));
        return Result<std::vector<FileNode>>::failure(
            Error(ErrorCode::NotFound, PathUtf8::toUtf8(directory) + " is not a directory"));
    }

    std::vector<FileNode> files;
    try
    {
        for (const auto& entry : fs::directory_iterator(target, fs::directory_options::skip_permission_denied))
        {
            auto node = buildFileNode(directory / entry.path().filename(), entry);
            if (node.hasValue())
            {
                files.push_back(std::move(node).value());
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        const Error error = toError(e);
        Logging::warn("listDirectory failed: " + std::string(toLogString(error.code)));
        return Result<std::vector<FileNode>>::failure(error);
    }

    Logging::debug("listDirectory succeeded: " + std::to_string(files.size()) + " entr" +
                    (files.size() == 1 ? "y" : "ies"));
    return Result<std::vector<FileNode>>::success(std::move(files));
}

Result<std::vector<FileNode>> StandardFileSystemRepository::listDirectoryRecursive(
    const std::filesystem::path& root, const std::function<void(const std::vector<FileNode>&)>& onProgress,
    const std::function<bool(const std::filesystem::path&)>& shouldPrune) const
{
    if (root == VirtualPaths::ThisPC)
    {
        return Result<std::vector<FileNode>>::failure(
            Error(ErrorCode::InvalidArgument, "Cannot recursively search This PC"));
    }

    if (ArchivePathResolver::resolve(root))
    {
        return Result<std::vector<FileNode>>::failure(
            Error(ErrorCode::InvalidArgument, "Cannot recursively search inside an archive"));
    }

    const fs::path target = withPrefix(root);

    std::error_code ec;
    if (!fs::is_directory(target, ec))
    {
        Logging::warn("listDirectoryRecursive failed: " + std::string(toLogString(ErrorCode::NotFound)));
        return Result<std::vector<FileNode>>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(root) + " is not a directory"));
    }

    std::vector<FileNode> files;
    // Throttled to ~once/second (Architecture.md §14.37) so a caller reporting progress back to a
    // Qt UI thread doesn't flood it with a signal/model-refresh per file discovered.
    auto lastProgressTime = std::chrono::steady_clock::now();
    const auto scanStart = std::chrono::steady_clock::now();
    try
    {
        // Explicit-iterator loop (rather than a range-based for) so disable_recursion_pending() is
        // reachable below -- the actual I/O-avoidance payoff of shouldPrune (Architecture.md
        // §14.45): a pruned directory is never descended into, not just filtered out afterwards.
        auto it = fs::recursive_directory_iterator(target, fs::directory_options::skip_permission_denied);
        const auto end = fs::end(it);
        for (; it != end; )
        {
            const auto& entry = *it;

            // fs::relative() canonicalizes both paths, which resolves symlinks (including the
            // entry's own final path component if it is itself a symlink) — a symlink entry would
            // silently be remapped onto its target's path/name instead of keeping its own.
            // lexically_relative() is purely textual and never touches the filesystem.
            const fs::path relative = entry.path().lexically_relative(target);
            const fs::path outwardPath = root / relative;

            if (shouldPrune && shouldPrune(outwardPath))
            {
                std::error_code isDirEc;
                if (entry.is_directory(isDirEc))
                {
                    it.disable_recursion_pending();
                }
                continue;
            }

            auto node = buildFileNode(outwardPath, entry);
            if (node.hasValue())
            {
                files.push_back(std::move(node).value());
            }

            if (onProgress)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastProgressTime >= std::chrono::seconds(1))
                {
                    onProgress(files);
                    lastProgressTime = now;
                }
            }

            // Non-throwing increment: the entry (or a directory just descended into) can vanish out
            // from under the walk -- deleted, renamed, or moved by another process mid-scan, which
            // surfaces as a "path not found" error rather than a permission error, so
            // skip_permission_denied above doesn't cover it. Stop here and keep whatever was already
            // found instead of discarding the whole scan over one transient entry (this is what was
            // throwing filesystem_error into the catch below and turning a long drive scan into a
            // hard failure).
            std::error_code incrementEc;
            it.increment(incrementEc);
            if (incrementEc)
            {
                Logging::warn("listDirectoryRecursive: stopped early (" + incrementEc.message() + ") after " +
                               std::to_string(files.size()) + " entries");
                break;
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        const Error error = toError(e);
        Logging::warn("listDirectoryRecursive failed: " + std::string(toLogString(error.code)));
        return Result<std::vector<FileNode>>::failure(error);
    }

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - scanStart).count();
    Logging::info("listDirectoryRecursive succeeded: " + std::to_string(files.size()) + " entries in " +
                   std::to_string(elapsedMs) + "ms");
    return Result<std::vector<FileNode>>::success(std::move(files));
}

Result<FileNode> StandardFileSystemRepository::stat(const std::filesystem::path& path) const
{
    if (path == VirtualPaths::ThisPC)
    {
        auto node = FileNode::create(path, 0, std::chrono::system_clock::now(), std::chrono::system_clock::now(),
                                      FileType::Directory);
        if (!node)
        {
            return node;
        }
        return Result<FileNode>::success(node.value().withDisplayName("This PC"));
    }

    if (auto resolution = ArchivePathResolver::resolve(path); resolution && !resolution->entryPathInArchive.empty())
    {
        return statArchiveEntry(path, *resolution);
    }

    const fs::path target = withPrefix(path);

    std::error_code ec;
    if (!fs::exists(target, ec))
    {
        return Result<FileNode>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(path) + " does not exist"));
    }

    return buildFileNode(path, makeDirectoryEntry(target));
}

namespace
{
    // Defense-in-depth for the read-only-archive guards below -- the UI also disables these
    // actions proactively (Architecture.md §14.28).
    Result<void> readOnlyArchiveError()
    {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "Cannot modify a read-only archive"));
    }

    bool isInsideArchive(const std::filesystem::path& path)
    {
        auto resolution = ArchivePathResolver::resolve(path);
        return resolution && !resolution->entryPathInArchive.empty();
    }
}

Result<FileNode> StandardFileSystemRepository::move(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    if (isInsideArchive(source) || isInsideArchive(destination))
    {
        return Result<FileNode>::failure(readOnlyArchiveError().error());
    }

    try
    {
        fs::rename(withPrefix(source), withPrefix(destination));
    }
    catch (const fs::filesystem_error& e)
    {
        const Error error = toError(e);
        Logging::warn("move failed: " + std::string(toLogString(error.code)));
        return Result<FileNode>::failure(error);
    }

    Logging::debug("move succeeded");
    return buildFileNode(destination, makeDirectoryEntry(withPrefix(destination)));
}

Result<FileNode> StandardFileSystemRepository::copy(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    // A destination inside an archive is always rejected, even though source-inside-archive is a
    // legitimate extraction request below -- an archive is read-only regardless of which operand
    // names it.
    if (isInsideArchive(destination))
    {
        return Result<FileNode>::failure(readOnlyArchiveError().error());
    }

    if (auto resolution = ArchivePathResolver::resolve(source); resolution && !resolution->entryPathInArchive.empty())
    {
        auto extracted = LibArchiveReader::extractEntries(resolution->archiveFile, resolution->entryPathInArchive, destination);
        if (!extracted)
        {
            return Result<FileNode>::failure(std::move(extracted).error());
        }
        return stat(destination);
    }

    try
    {
        fs::copy(withPrefix(source), withPrefix(destination), fs::copy_options::recursive);
    }
    catch (const fs::filesystem_error& e)
    {
        const Error error = toError(e);
        Logging::warn("copy failed: " + std::string(toLogString(error.code)));
        return Result<FileNode>::failure(error);
    }

    Logging::debug("copy succeeded");
    return buildFileNode(destination, makeDirectoryEntry(withPrefix(destination)));
}

Result<void> StandardFileSystemRepository::deletePermanently(const std::filesystem::path& path)
{
    if (isInsideArchive(path))
    {
        return readOnlyArchiveError();
    }

    std::error_code ec;
    fs::remove_all(withPrefix(path), ec);
    if (ec)
    {
        Logging::warn("deletePermanently failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, ec.message()));
    }

    Logging::debug("deletePermanently succeeded");
    return Result<void>::success();
}

Result<void> StandardFileSystemRepository::moveToTrash(const std::filesystem::path& path)
{
    if (isInsideArchive(path))
    {
        return readOnlyArchiveError();
    }

#ifdef _WIN32
    std::wstring nativePath = path.wstring();
    nativePath.push_back(L'\0'); // SHFileOperationW requires a double-null-terminated list.

    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = nativePath.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to move file to trash"));
    }

    return Result<void>::success();
#else
    return deletePermanently(path);
#endif
}

Result<void> StandardFileSystemRepository::openWithDefaultApplication(const std::filesystem::path& path)
{
    if (auto resolution = ArchivePathResolver::resolve(path); resolution && !resolution->entryPathInArchive.empty())
    {
        return openArchiveEntryWithDefaultApplication(*resolution);
    }

#ifdef _WIN32
    const std::wstring nativePath = withPrefix(path).wstring();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", nativePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecute returns a value > 32 on success (Windows API convention).
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to open file with default application"));
    }
    return Result<void>::success();
#else
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}

Result<std::uint64_t> StandardFileSystemRepository::computeFileHash(const FileNode& file) const
{
    const fs::path target = withPrefix(file.path());

    std::ifstream stream(target, std::ios::binary);
    if (!stream)
    {
        return Result<std::uint64_t>::failure(Error(ErrorCode::IoError, "Failed to open file for hashing"));
    }

    constexpr std::size_t chunkSize = 64 * 1024;
    std::array<char, chunkSize> buffer{};

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    const auto mtime = file.modificationDate().time_since_epoch().count();
    XXH64_update(state, &mtime, sizeof(mtime));

    const auto size = file.size();
    XXH64_update(state, &size, sizeof(size));

    stream.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
    XXH64_update(state, buffer.data(), static_cast<std::size_t>(stream.gcount()));

    if (size > chunkSize)
    {
        stream.seekg(-static_cast<std::streamoff>(chunkSize), std::ios::end);
        stream.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
        XXH64_update(state, buffer.data(), static_cast<std::size_t>(stream.gcount()));
    }

    const std::uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    return Result<std::uint64_t>::success(hash);
}

Result<void> StandardFileSystemRepository::createDirectory(const std::filesystem::path& directory)
{
    if (isInsideArchive(directory))
    {
        return readOnlyArchiveError();
    }

    const fs::path target = withPrefix(directory);

    std::error_code ec;
    if (fs::exists(target, ec))
    {
        return Result<void>::failure(Error(ErrorCode::AlreadyExists, PathUtf8::toUtf8(directory) + " already exists"));
    }

    ec.clear();
    if (!fs::create_directory(target, ec) || ec)
    {
        Logging::warn("createDirectory failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<void>::failure(Error(ErrorCode::IoError, ec.message()));
    }

    Logging::debug("createDirectory succeeded");
    return Result<void>::success();
}

Result<FileNode> StandardFileSystemRepository::createFileFromTemplate(const std::filesystem::path& destinationFile,
                                                                        const std::optional<std::filesystem::path>& templateFile)
{
    if (isInsideArchive(destinationFile))
    {
        return Result<FileNode>::failure(readOnlyArchiveError().error());
    }

    const fs::path target = withPrefix(destinationFile);

    std::error_code ec;
    if (fs::exists(target, ec))
    {
        return Result<FileNode>::failure(Error(ErrorCode::AlreadyExists, PathUtf8::toUtf8(destinationFile) + " already exists"));
    }

    try
    {
        if (templateFile)
        {
            fs::copy_file(withPrefix(*templateFile), target);
        }
        else
        {
            std::ofstream stream(target, std::ios::binary);
            if (!stream)
            {
                return Result<FileNode>::failure(Error(ErrorCode::IoError, "Failed to create " + PathUtf8::toUtf8(destinationFile)));
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        return Result<FileNode>::failure(toError(e));
    }

    return buildFileNode(destinationFile, makeDirectoryEntry(target));
}

Result<void> StandardFileSystemRepository::showProperties(const std::filesystem::path& path, NativeWindowHandle ownerWindow)
{
#ifdef _WIN32
    const std::wstring nativePath = withPrefix(path).wstring();

    // Unlike ShellExecuteW/SHFileOperationW elsewhere in this file, SHObjectProperties does not
    // auto-initialize COM on the calling thread — it just returns FALSE if COM isn't already
    // initialized there, which is why this call otherwise always fails.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comInit) && comInit != RPC_E_CHANGED_MODE)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to initialize COM for showProperties"));
    }

    const BOOL ok = SHObjectProperties(static_cast<HWND>(ownerWindow), SHOP_FILEPATH, nativePath.c_str(), nullptr);

    if (SUCCEEDED(comInit))
    {
        CoUninitialize();
    }

    if (!ok)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to show properties for " + PathUtf8::toUtf8(path)));
    }
    return Result<void>::success();
#else
    (void)path;
    (void)ownerWindow;
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}

Result<std::vector<std::byte>> StandardFileSystemRepository::readFilePrefix(const std::filesystem::path& path,
                                                                              std::size_t maxBytes) const
{
    const fs::path target = withPrefix(path);

    std::ifstream stream(target, std::ios::binary);
    if (!stream)
    {
        return Result<std::vector<std::byte>>::failure(
            Error(ErrorCode::IoError, "Failed to open " + PathUtf8::toUtf8(path) + " for reading"));
    }

    std::vector<std::byte> buffer(maxBytes);
    stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(maxBytes));
    buffer.resize(static_cast<std::size_t>(stream.gcount()));

    return Result<std::vector<std::byte>>::success(std::move(buffer));
}

Result<std::vector<FileNode>> StandardFileSystemRepository::listThisPc() const
{
#ifdef _WIN32
    std::vector<FileNode> entries;

    const auto addKnownFolder = [&entries](REFKNOWNFOLDERID id) {
        auto node = buildKnownFolderNode(id);
        if (node.hasValue())
        {
            entries.push_back(std::move(node).value());
        }
    };
    addKnownFolder(FOLDERID_Downloads);
    addKnownFolder(FOLDERID_Documents);
    addKnownFolder(FOLDERID_Pictures);
    addKnownFolder(FOLDERID_Videos);
    addKnownFolder(FOLDERID_Music);
    addKnownFolder(FOLDERID_Desktop);

    const DWORD presentDrives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if ((presentDrives & (1u << i)) == 0)
        {
            continue;
        }

        if (auto node = buildDriveNode(static_cast<wchar_t>(L'A' + i)))
        {
            entries.push_back(std::move(*node));
        }
    }

    return Result<std::vector<FileNode>>::success(std::move(entries));
#else
    return Result<std::vector<FileNode>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}

std::chrono::system_clock::time_point StandardFileSystemRepository::archiveModificationTime(
    const std::filesystem::path& archiveFile) const
{
    std::error_code ec;
    const auto mtime = fs::last_write_time(archiveFile, ec);
    return ec ? std::chrono::system_clock::time_point{} : std::chrono::clock_cast<std::chrono::system_clock>(mtime);
}

namespace
{
    // Aggregated state for one immediate child of an archive-rooted directory. A directory is
    // "synthesized" (isDirectory but no matching leaf entry ever set size/modificationTime) when
    // the archive format doesn't carry explicit directory entries for it (common for zip).
    struct ArchiveChildInfo
    {
        bool isDirectory = false;
        std::uintmax_t size = 0;
        std::chrono::system_clock::time_point modificationTime;
    };
}

Result<std::vector<FileNode>> StandardFileSystemRepository::listArchiveDirectory(
    const std::filesystem::path& directory, const ArchivePathResolver::Resolution& resolution) const
{
    auto listed = m_archiveIndexCache.entriesFor(resolution.archiveFile);
    if (!listed)
    {
        return Result<std::vector<FileNode>>::failure(std::move(listed).error());
    }

    const fs::path& prefix = resolution.entryPathInArchive;
    const auto fallbackModTime = archiveModificationTime(resolution.archiveFile);

    std::map<fs::path, ArchiveChildInfo> children;
    bool prefixIsExplicitDirectory = false;
    bool prefixIsExplicitFile = false;

    for (const auto& entry : listed.value())
    {
        if (entry.relativePath == prefix)
        {
            // The prefix's own entry, not one of its children. A *file* entry matching prefix
            // means prefix names a file, not a folder -- not listable, even though stat() on the
            // same path succeeds (see statArchiveEntry).
            if (entry.isDirectory)
            {
                prefixIsExplicitDirectory = true;
            }
            else
            {
                prefixIsExplicitFile = true;
            }
            continue;
        }

        const fs::path relative = prefix.empty() ? entry.relativePath : entry.relativePath.lexically_relative(prefix);
        if (relative.empty() || *relative.begin() == fs::path(".."))
        {
            continue; // Not a descendant of prefix.
        }

        auto componentIt = relative.begin();
        const fs::path childName = *componentIt;
        const bool hasMoreComponents = ++componentIt != relative.end();

        ArchiveChildInfo& info = children[childName];
        if (hasMoreComponents)
        {
            info.isDirectory = true;
        }
        else if (entry.isDirectory)
        {
            info.isDirectory = true;
        }
        else
        {
            info.size = entry.size;
            info.modificationTime = entry.modificationTime;
        }
    }

    // Listable when: it's the archive root; it has an explicit directory entry; or (zip-style,
    // no explicit directory entries) at least one descendant was found and prefix didn't also
    // match a *file* entry exactly.
    const bool listable = prefix.empty() || prefixIsExplicitDirectory || (!prefixIsExplicitFile && !children.empty());
    if (!listable)
    {
        return Result<std::vector<FileNode>>::failure(
            Error(ErrorCode::NotFound, PathUtf8::toUtf8(directory) + " does not exist in archive"));
    }

    std::vector<FileNode> result;
    result.reserve(children.size());
    for (const auto& [childName, info] : children)
    {
        const auto modTime = info.isDirectory ? fallbackModTime : info.modificationTime;
        auto node = FileNode::create(directory / childName, info.isDirectory ? 0 : info.size, modTime, modTime,
                                      info.isDirectory ? FileType::Directory : FileType::Regular);
        if (node.hasValue())
        {
            result.push_back(std::move(node).value());
        }
    }

    return Result<std::vector<FileNode>>::success(std::move(result));
}

Result<FileNode> StandardFileSystemRepository::statArchiveEntry(const std::filesystem::path& outwardPath,
                                                                   const ArchivePathResolver::Resolution& resolution) const
{
    auto listed = m_archiveIndexCache.entriesFor(resolution.archiveFile);
    if (!listed)
    {
        return Result<FileNode>::failure(std::move(listed).error());
    }

    const fs::path& prefix = resolution.entryPathInArchive;
    const auto fallbackModTime = archiveModificationTime(resolution.archiveFile);

    bool found = false;
    bool isDirectory = false;
    std::uintmax_t size = 0;
    auto modTime = fallbackModTime;

    for (const auto& entry : listed.value())
    {
        if (entry.relativePath == prefix)
        {
            found = true;
            if (entry.isDirectory)
            {
                isDirectory = true;
            }
            else
            {
                size = entry.size;
                modTime = entry.modificationTime;
            }
            continue;
        }

        if (!isDirectory)
        {
            const fs::path relative = entry.relativePath.lexically_relative(prefix);
            if (!relative.empty() && *relative.begin() != fs::path(".."))
            {
                // A descendant exists even without an explicit directory entry (zip-style).
                found = true;
                isDirectory = true;
            }
        }
    }

    if (!found)
    {
        return Result<FileNode>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(outwardPath) + " does not exist in archive"));
    }

    return FileNode::create(outwardPath, isDirectory ? 0 : size, modTime, modTime,
                             isDirectory ? FileType::Directory : FileType::Regular);
}

Result<void> StandardFileSystemRepository::openArchiveEntryWithDefaultApplication(
    const ArchivePathResolver::Resolution& resolution)
{
#ifdef _WIN32
    auto entryStat = statArchiveEntry(resolution.archiveFile / resolution.entryPathInArchive, resolution);
    if (!entryStat)
    {
        return Result<void>::failure(std::move(entryStat).error());
    }

    auto sessionDir = extractEntryToTempDir(resolution);
    if (!sessionDir)
    {
        return Result<void>::failure(std::move(sessionDir).error());
    }

    const fs::path openTarget = entryStat.value().isDirectory() ? sessionDir.value()
                                                                  : sessionDir.value() / resolution.entryPathInArchive.filename();

    const std::wstring nativePath = withPrefix(openTarget).wstring();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", nativePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to open extracted file"));
    }
    return Result<void>::success();
#else
    (void)resolution;
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}

Result<fs::path> StandardFileSystemRepository::extractEntryToTempDir(const ArchivePathResolver::Resolution& resolution) const
{
    std::error_code ec;
    const fs::path baseTemp = fs::temp_directory_path(ec);
    if (ec)
    {
        return Result<fs::path>::failure(Error(ErrorCode::IoError, "No temp directory available"));
    }

    // Unique per call (not cleaned up by the app -- relies on normal OS temp-dir lifecycle,
    // Architecture.md §14.28) so repeated extractions of the same/different entries never collide.
    const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path sessionDir = baseTemp / L"Exp-LORer" / L"archive-preview" /
                                 (std::to_wstring(fs::hash_value(resolution.entryPathInArchive)) + L"-" +
                                  std::to_wstring(static_cast<unsigned long long>(uniqueSuffix)));

    auto extracted = LibArchiveReader::extractEntries(resolution.archiveFile, resolution.entryPathInArchive, sessionDir);
    if (!extracted)
    {
        return Result<fs::path>::failure(std::move(extracted).error());
    }

    return Result<fs::path>::success(sessionDir);
}

Result<fs::path> StandardFileSystemRepository::materializeForReading(const std::filesystem::path& path) const
{
    auto resolution = ArchivePathResolver::resolve(path);
    if (!resolution || resolution->entryPathInArchive.empty())
    {
        return Result<fs::path>::success(path);
    }

    auto sessionDir = extractEntryToTempDir(*resolution);
    if (!sessionDir)
    {
        return sessionDir;
    }

    return Result<fs::path>::success(sessionDir.value() / resolution->entryPathInArchive.filename());
}

Result<void> StandardFileSystemRepository::extractArchive(const std::filesystem::path& archiveFile,
                                                             const std::filesystem::path& destinationDirectory)
{
    auto resolution = ArchivePathResolver::resolve(archiveFile);
    if (!resolution)
    {
        Logging::warn("extractArchive failed: " + std::string(toLogString(ErrorCode::InvalidArgument)));
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, PathUtf8::toUtf8(archiveFile) + " is not inside a known archive"));
    }

    auto result = LibArchiveReader::extractEntries(resolution->archiveFile, resolution->entryPathInArchive, destinationDirectory);
    if (!result)
    {
        Logging::warn("extractArchive failed: " + std::string(toLogString(result.error().code)));
    }
    else
    {
        Logging::debug("extractArchive succeeded");
    }
    return result;
}

Result<DriveSpaceInfo> StandardFileSystemRepository::driveSpaceInfo(const std::filesystem::path& path) const
{
#ifdef _WIN32
    const fs::path root = path.root_path();
    if (root.empty())
    {
        return Result<DriveSpaceInfo>::failure(Error(ErrorCode::NotFound, "No drive for " + PathUtf8::toUtf8(path)));
    }

    const std::wstring rootName = path.root_name().wstring();
    std::wstring rootWide;
    std::wstring displayLabel;

    // A lettered root -- root_name() is exactly "X:" regardless of how the caller spelled the
    // separator or whether one was even present at all ("C:/data", a bare "C:", and "C:\" all
    // share the same root_name()) -- mirrors buildDriveNode() via the shared resolveDriveLabel()
    // helper for the same "Local Disk (C:)"-style text. A non-lettered root (a UNC path typed
    // directly into the address bar, "\\server\share\") skips driveDisplayLabel -- it assumes a
    // drive-letter suffix -- and falls back to the volume label or the raw UNC root text.
    if (rootName.size() == 2 && rootName[1] == L':')
    {
        const wchar_t letter = rootName[0];
        rootWide = std::wstring(1, letter) + L":\\";

        auto label = resolveDriveLabel(letter);
        if (!label)
        {
            return Result<DriveSpaceInfo>::failure(Error(ErrorCode::NotFound, "No drive for " + PathUtf8::toUtf8(path)));
        }
        displayLabel = std::move(*label);
    }
    else
    {
        rootWide = root.wstring();
        wchar_t volumeName[MAX_PATH + 1] = {};
        const BOOL gotVolumeInfo = GetVolumeInformationW(rootWide.c_str(), volumeName,
                                                           static_cast<DWORD>(sizeof(volumeName) / sizeof(wchar_t)),
                                                           nullptr, nullptr, nullptr, nullptr, 0);
        displayLabel = gotVolumeInfo && volumeName[0] != L'\0' ? std::wstring(volumeName) : rootWide;
    }

    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    if (!GetDiskFreeSpaceExW(rootWide.c_str(), &freeBytes, &totalBytes, nullptr))
    {
        Logging::warn("driveSpaceInfo failed: " + std::string(toLogString(ErrorCode::IoError)));
        return Result<DriveSpaceInfo>::failure(Error(ErrorCode::IoError, "Failed to query free space for " + PathUtf8::toUtf8(root)));
    }

    DriveSpaceInfo info;
    info.displayLabel = utf8FromWide(displayLabel);
    info.freeBytes = freeBytes.QuadPart;
    info.totalBytes = totalBytes.QuadPart;
    return Result<DriveSpaceInfo>::success(std::move(info));
#else
    (void)path;
    return Result<DriveSpaceInfo>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
#endif
}

Result<FileNode> StandardFileSystemRepository::copyFileWithProgress(
    const std::filesystem::path& source, const std::filesystem::path& destination,
    const std::function<bool(std::uint64_t bytesCopiedSoFar)>& onChunk)
{
    if (isInsideArchive(source) || isInsideArchive(destination))
    {
        return Result<FileNode>::failure(readOnlyArchiveError().error());
    }

    const fs::path src = withPrefix(source);
    const fs::path dst = withPrefix(destination);

#ifdef _WIN32
    LPVOID progressContext = const_cast<void*>(static_cast<const void*>(&onChunk));
    const BOOL ok = CopyFileExW(src.c_str(), dst.c_str(), copyProgressTrampoline, progressContext, nullptr, 0);
    if (!ok)
    {
        const Error error = win32CopyError(GetLastError());
        Logging::warn("copyFileWithProgress failed: " + std::string(toLogString(error.code)));
        return Result<FileNode>::failure(error);
    }

    Logging::debug("copyFileWithProgress succeeded");
    return buildFileNode(destination, makeDirectoryEntry(dst));
#else
    std::ifstream input(src, std::ios::binary);
    if (!input)
    {
        return Result<FileNode>::failure(Error(ErrorCode::IoError, "Failed to open " + PathUtf8::toUtf8(source) + " for reading"));
    }

    std::ofstream output(dst, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return Result<FileNode>::failure(Error(ErrorCode::IoError, "Failed to open " + PathUtf8::toUtf8(destination) + " for writing"));
    }

    constexpr std::size_t chunkSize = 1024 * 1024;
    std::vector<char> buffer(chunkSize);
    std::uint64_t totalCopied = 0;

    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(chunkSize));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead <= 0)
        {
            break;
        }

        output.write(buffer.data(), bytesRead);
        totalCopied += static_cast<std::uint64_t>(bytesRead);

        if (onChunk && !onChunk(totalCopied))
        {
            output.close();
            std::error_code ec;
            fs::remove(dst, ec);
            return Result<FileNode>::failure(Error(ErrorCode::Cancelled, "Copy cancelled"));
        }
    }

    output.close();
    return buildFileNode(destination, makeDirectoryEntry(dst));
#endif
}
