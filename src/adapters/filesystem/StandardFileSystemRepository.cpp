#include "StandardFileSystemRepository.h"

#include <array>
#include <chrono>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>

#include <Shellapi.h>
#endif

#include <xxhash.h>

namespace
{
    namespace fs = std::filesystem;

    fs::path withLongPathPrefix(const fs::path& path)
    {
#ifdef _WIN32
        const std::wstring native = path.wstring();
        constexpr std::size_t maxPath = 260;
        if (native.size() < maxPath || native.rfind(LR"(\\?\)", 0) == 0)
        {
            return path;
        }
        return fs::path(LR"(\\?\)" + native);
#else
        return path;
#endif
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

    // std::filesystem exposes only last-write time portably; creation time is approximated with
    // it since std::filesystem has no portable creation-time accessor.
    Result<FileNode> buildFileNode(const fs::path& outwardPath, const fs::directory_entry& entry)
    {
        std::error_code ec;
        const auto size = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
        ec.clear();
        const auto lastWrite = entry.last_write_time(ec);
        if (ec)
        {
            return Result<FileNode>::failure(Error(ErrorCode::IoError, ec.message()));
        }

        const auto modificationTime = std::chrono::clock_cast<std::chrono::system_clock>(lastWrite);
        return FileNode::create(outwardPath, size, modificationTime, modificationTime, toFileType(entry));
    }

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
}

Result<std::vector<FileNode>> StandardFileSystemRepository::listDirectory(const std::filesystem::path& directory) const
{
    const fs::path target = withLongPathPrefix(directory);

    std::error_code ec;
    if (!fs::is_directory(target, ec))
    {
        return Result<std::vector<FileNode>>::failure(
            Error(ErrorCode::NotFound, directory.string() + " is not a directory"));
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
        return Result<std::vector<FileNode>>::failure(toError(e));
    }

    return Result<std::vector<FileNode>>::success(std::move(files));
}

Result<FileNode> StandardFileSystemRepository::move(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    try
    {
        fs::rename(withLongPathPrefix(source), withLongPathPrefix(destination));
    }
    catch (const fs::filesystem_error& e)
    {
        return Result<FileNode>::failure(toError(e));
    }

    return buildFileNode(destination, fs::directory_entry(withLongPathPrefix(destination)));
}

Result<FileNode> StandardFileSystemRepository::copy(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    try
    {
        fs::copy(withLongPathPrefix(source), withLongPathPrefix(destination), fs::copy_options::recursive);
    }
    catch (const fs::filesystem_error& e)
    {
        return Result<FileNode>::failure(toError(e));
    }

    return buildFileNode(destination, fs::directory_entry(withLongPathPrefix(destination)));
}

Result<void> StandardFileSystemRepository::deletePermanently(const std::filesystem::path& path)
{
    std::error_code ec;
    fs::remove_all(withLongPathPrefix(path), ec);
    if (ec)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, ec.message()));
    }

    return Result<void>::success();
}

Result<void> StandardFileSystemRepository::moveToTrash(const std::filesystem::path& path)
{
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

Result<std::uint64_t> StandardFileSystemRepository::computeFileHash(const FileNode& file) const
{
    const fs::path target = withLongPathPrefix(file.path());

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
