#include "LibArchiveReader.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <fstream>
#include <optional>
#include <system_error>

#include "PathUtf8.h"
#include "WindowsLongPath.h"

namespace
{
    namespace fs = std::filesystem;

    struct ReaderGuard
    {
        archive* handle;
        ~ReaderGuard() { archive_read_free(handle); }
    };

    Error archiveError(archive* a, const std::string& fallback)
    {
        const char* message = archive_error_string(a);
        return Error(ErrorCode::IoError, message ? message : fallback);
    }

    // Enables only the formats/filters in scope (Architecture.md §14.28) rather than
    // archive_read_support_format_all(), and opens with the wide-char entry point on Windows so
    // non-ASCII archive paths never round-trip through a narrow/ANSI string.
    archive* openReader(const std::filesystem::path& archiveFile, Result<void>& outError)
    {
        archive* a = archive_read_new();
        archive_read_support_format_zip(a);
        archive_read_support_format_7zip(a);
        archive_read_support_format_tar(a);
        archive_read_support_format_gnutar(a);
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
        // Deliberately NOT archive_read_support_format_raw(): it treats any file's bytes as one
        // opaque "raw" entry regardless of content, so it would make every file -- not just
        // archives -- look like a successfully-opened archive.
        archive_read_support_filter_gzip(a);
        archive_read_support_filter_bzip2(a);

        const fs::path target = WindowsLongPath::withPrefix(archiveFile);

#ifdef _WIN32
        const int rc = archive_read_open_filename_w(a, target.c_str(), 64 * 1024);
#else
        const int rc = archive_read_open_filename(a, PathUtf8::toUtf8(target).c_str(), 64 * 1024);
#endif
        if (rc != ARCHIVE_OK)
        {
            outError = Result<void>::failure(archiveError(a, "Failed to open archive"));
            archive_read_free(a);
            return nullptr;
        }

        return a;
    }

    fs::path normalizedEntryPath(archive_entry* entry)
    {
        std::wstring raw;
        if (const wchar_t* wide = archive_entry_pathname_w(entry))
        {
            raw = wide;
        }
        else if (const char* narrow = archive_entry_pathname(entry))
        {
            // Rare fallback (libarchive couldn't produce a wide-char name for this entry) --
            // reinterpreted as UTF-8 rather than the process's narrow code page.
            const std::u8string utf8(reinterpret_cast<const char8_t*>(narrow));
            raw = fs::path(utf8).wstring();
        }

        while (!raw.empty() && (raw.back() == L'/' || raw.back() == L'\\'))
        {
            raw.pop_back();
        }
        return fs::path(raw);
    }

    // True when relative is a strict, non-parent descendant (not ".." and not empty).
    bool isDescendant(const fs::path& relative)
    {
        return !relative.empty() && *relative.begin() != fs::path("..");
    }

    // Rejects any entry path that could escape the destination directory it's about to be joined
    // onto: absolute/rooted paths, or a ".." component anywhere (not just a leading one). A
    // malicious or corrupted archive can claim any entry name it likes -- this is the only thing
    // standing between that and writing outside the chosen extraction directory (Zip Slip).
    bool isSafeRelativePath(const fs::path& relative)
    {
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        {
            return false;
        }
        for (const auto& component : relative)
        {
            if (component == fs::path(".."))
            {
                return false;
            }
        }
        return true;
    }
}

Result<std::vector<LibArchiveReader::Entry>> LibArchiveReader::listEntries(const std::filesystem::path& archiveFile)
{
    Result<void> openError = Result<void>::success();
    archive* a = openReader(archiveFile, openError);
    if (!a)
    {
        return Result<std::vector<Entry>>::failure(std::move(openError).error());
    }
    ReaderGuard guard{ a };

    std::vector<Entry> entries;
    archive_entry* entry = nullptr;
    int rc;
    while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK)
    {
        const fs::path relativePath = normalizedEntryPath(entry);
        if (isSafeRelativePath(relativePath))
        {
            const bool isDir = archive_entry_filetype(entry) == AE_IFDIR;
            const auto size = static_cast<std::uintmax_t>(archive_entry_size(entry));
            const auto modTime = std::chrono::system_clock::from_time_t(archive_entry_mtime(entry));
            entries.push_back(Entry{ relativePath, size, modTime, isDir });
        }

        archive_read_data_skip(a);
    }

    if (rc != ARCHIVE_EOF)
    {
        return Result<std::vector<Entry>>::failure(archiveError(a, "Failed to read archive entries"));
    }

    return Result<std::vector<Entry>>::success(std::move(entries));
}

Result<void> LibArchiveReader::extractEntries(const std::filesystem::path& archiveFile, const std::filesystem::path& entryPrefix,
                                                const std::filesystem::path& destinationDirectory)
{
    Result<void> openError = Result<void>::success();
    archive* a = openReader(archiveFile, openError);
    if (!a)
    {
        return openError;
    }
    ReaderGuard guard{ a };

    const fs::path destination = WindowsLongPath::withPrefix(destinationDirectory);
    std::error_code ec;
    fs::create_directories(destination, ec);

    archive_entry* entry = nullptr;
    int rc;
    bool matchedPrefix = false;
    while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK)
    {
        const fs::path relativePath = normalizedEntryPath(entry);
        if (!isSafeRelativePath(relativePath))
        {
            archive_read_data_skip(a);
            continue;
        }

        const bool isDir = archive_entry_filetype(entry) == AE_IFDIR;

        std::optional<fs::path> destPath;
        bool writeData = false;

        if (entryPrefix.empty())
        {
            destPath = destination / relativePath;
            writeData = !isDir;
        }
        else if (relativePath == entryPrefix)
        {
            matchedPrefix = true;
            if (isDir)
            {
                destPath = destination;
            }
            else
            {
                destPath = destination / entryPrefix.filename();
                writeData = true;
            }
        }
        else
        {
            const fs::path relative = relativePath.lexically_relative(entryPrefix);
            if (isDescendant(relative))
            {
                matchedPrefix = true;
                destPath = destination / relative;
                writeData = !isDir;
            }
        }

        if (!destPath)
        {
            archive_read_data_skip(a);
            continue;
        }

        if (isDir)
        {
            fs::create_directories(*destPath, ec);
            archive_read_data_skip(a);
            continue;
        }

        fs::create_directories(destPath->parent_path(), ec);

        if (!writeData)
        {
            archive_read_data_skip(a);
            continue;
        }

        std::ofstream out(*destPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create " + PathUtf8::toUtf8(*destPath)));
        }

        const void* buffer = nullptr;
        std::size_t size = 0;
        la_int64_t offset = 0;
        int readRc;
        while ((readRc = archive_read_data_block(a, &buffer, &size, &offset)) == ARCHIVE_OK)
        {
            out.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(size));
            if (!out)
            {
                return Result<void>::failure(Error(ErrorCode::IoError, "Failed to write " + PathUtf8::toUtf8(*destPath)));
            }
        }
        if (readRc != ARCHIVE_EOF)
        {
            return Result<void>::failure(archiveError(a, "Failed to extract " + PathUtf8::toUtf8(relativePath)));
        }

        out.close();
        if (!out)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to finalize " + PathUtf8::toUtf8(*destPath)));
        }
    }

    if (rc != ARCHIVE_EOF)
    {
        return Result<void>::failure(archiveError(a, "Failed to read archive entries"));
    }

    if (!entryPrefix.empty() && !matchedPrefix)
    {
        return Result<void>::failure(Error(ErrorCode::NotFound, PathUtf8::toUtf8(entryPrefix) + " does not exist in archive"));
    }

    return Result<void>::success();
}
